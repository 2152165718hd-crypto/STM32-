#include ".\Hardware\FM225\FM225.h"

/* ==================== 私有变量 ==================== */

static UART_HandleTypeDef huart2_fm225; /* 模块内部 UART 句柄 */

/* 环形接收缓冲区 */
static volatile uint8_t rx_buf[FM225_RX_BUF_SIZE];
static volatile uint16_t rx_head = 0;
static volatile uint16_t rx_tail = 0;

/* 中断单字节接收 */
static uint8_t rx_byte;

/* 帧接收缓冲区 */
static uint8_t recv_buf[FM225_MAX_RESPONSE_SIZE];

/* 当前活动命令 */
static FM225_Command_t active_command = FM225_CMD_NONE;
static uint32_t active_cmd_deadline = 0;
static bool enrolling = false;
static volatile bool uart_error_pending = false;
static volatile bool rx_overflow = false;
static uint32_t rx_last_tick = 0;

/* 延迟执行队列（简易：最多缓存 1 条待发命令） */
static FM225_Command_t deferred_cmd = FM225_CMD_NONE;

/* 上一次 REPLY 帧诊断缓存 */
static uint8_t  last_reply[16];
static uint16_t last_reply_len = 0;

/* 用户回调指针 */
static FM225_MatchedCb_t cb_matched = NULL;
static FM225_UnmatchedCb_t cb_unmatched = NULL;
static FM225_InvalidCb_t cb_invalid = NULL;
static FM225_EnrollDoneCb_t cb_enroll_done = NULL;
static FM225_EnrollFailCb_t cb_enroll_fail = NULL;
static FM225_FaceInfoCb_t cb_face_info = NULL;
static FM225_FaceCountCb_t cb_face_count = NULL;

/* ==================== 私有函数声明 ==================== */

static void FM225_UART_Init(void);
static void FM225_RingBuf_Push(uint8_t byte);
static bool FM225_RingBuf_Pop(uint8_t *byte);
static uint16_t FM225_RingBuf_Available(void);
static void FM225_SendCommand(FM225_Command_t cmd, const uint8_t *data, uint16_t size);
static void FM225_RecvCommand(void);
static void FM225_HandleNote(const uint8_t *data, uint16_t length);
static void FM225_HandleReply(const uint8_t *data, uint16_t length);
static void FM225_RingBuf_Reset(void);
static void FM225_UART_Recover(void);
static uint32_t FM225_GetCommandTimeoutMs(FM225_Command_t cmd);
static bool FM225_IsExpired(uint32_t deadline);
static void FM225_HandleCommandTimeout(FM225_Command_t cmd);

/* ==================== USART2 硬件初始化 ==================== */

static void FM225_UART_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* 使能时钟 */
    __HAL_RCC_USART2_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* PA2 -> USART2_TX (复用推挽) */
    GPIO_InitStruct.Pin = FM225_TX_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(FM225_GPIO_PORT, &GPIO_InitStruct);

    /* PA3 -> USART2_RX (浮空输入) */
    GPIO_InitStruct.Pin = FM225_RX_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(FM225_GPIO_PORT, &GPIO_InitStruct);

    /* UART 参数 */
    huart2_fm225.Instance = FM225_UART;
    huart2_fm225.Init.BaudRate = FM225_UART_BAUD;
    huart2_fm225.Init.WordLength = UART_WORDLENGTH_8B;
    huart2_fm225.Init.StopBits = UART_STOPBITS_1;
    huart2_fm225.Init.Parity = UART_PARITY_NONE;
    huart2_fm225.Init.Mode = UART_MODE_TX_RX;
    huart2_fm225.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart2_fm225.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart2_fm225) != HAL_OK)
    {
        return;
    }

    /* 使能 NVIC 中断 */
    HAL_NVIC_SetPriority(FM225_UART_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(FM225_UART_IRQn);

    /* 启动中断接收（单字节） */
    FM225_RingBuf_Reset();
    uart_error_pending = false;
    rx_overflow = false;
    rx_last_tick = HAL_GetTick();
    HAL_UART_Receive_IT(&huart2_fm225, &rx_byte, 1);
}

/* ==================== 中断服务函数 ==================== */

/**
 * @brief USART2 中断入口
 */
void USART2_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart2_fm225);
}

/**
 * @brief UART 接收完成回调，由用户在 HAL_UART_RxCpltCallback 中调用
 */
void FM225_RxCallback(void)
{
    FM225_RingBuf_Push(rx_byte);
    rx_last_tick = HAL_GetTick();
    HAL_UART_Receive_IT(&huart2_fm225, &rx_byte, 1);
}

void FM225_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart == NULL || huart->Instance != FM225_UART)
    {
        return;
    }
    uart_error_pending = true;
}

/* ==================== 环形缓冲区 ==================== */

static void FM225_RingBuf_Push(uint8_t byte)
{
    uint16_t next = (rx_head + 1) % FM225_RX_BUF_SIZE;
    if (next != rx_tail) /* 满则丢弃 */
    {
        rx_buf[rx_head] = byte;
        rx_head = next;
    }
    else
    {
        rx_overflow = true;
    }
}

static bool FM225_RingBuf_Pop(uint8_t *byte)
{
    if (rx_tail == rx_head)
        return false;
    *byte = rx_buf[rx_tail];
    rx_tail = (rx_tail + 1) % FM225_RX_BUF_SIZE;
    return true;
}

static uint16_t FM225_RingBuf_Available(void)
{
    return (rx_head + FM225_RX_BUF_SIZE - rx_tail) % FM225_RX_BUF_SIZE;
}

static void FM225_RingBuf_Reset(void)
{
    rx_head = 0;
    rx_tail = 0;
}

/* ==================== 发送命令与恢复 ==================== */

static uint32_t FM225_GetCommandTimeoutMs(FM225_Command_t cmd)
{
    return (cmd == FM225_CMD_RESET) ? FM225_RESET_TIMEOUT_MS : FM225_CMD_TIMEOUT_MS;
}

static bool FM225_IsExpired(uint32_t deadline)
{
    return ((int32_t)(HAL_GetTick() - deadline) >= 0);
}

static void FM225_UART_Recover(void)
{
    __HAL_UART_DISABLE_IT(&huart2_fm225, UART_IT_RXNE);
    __HAL_UART_DISABLE_IT(&huart2_fm225, UART_IT_PE);
    __HAL_UART_DISABLE_IT(&huart2_fm225, UART_IT_ERR);

    __HAL_UART_CLEAR_PEFLAG(&huart2_fm225);
    __HAL_UART_CLEAR_FEFLAG(&huart2_fm225);
    __HAL_UART_CLEAR_NEFLAG(&huart2_fm225);
    __HAL_UART_CLEAR_OREFLAG(&huart2_fm225);

    huart2_fm225.ErrorCode = HAL_UART_ERROR_NONE;
    huart2_fm225.RxState = HAL_UART_STATE_READY;
    (void)HAL_UART_Receive_IT(&huart2_fm225, &rx_byte, 1);
}

static void FM225_HandleCommandTimeout(FM225_Command_t cmd)
{
    if (cmd == FM225_CMD_NONE)
    {
        active_cmd_deadline = 0;
        enrolling = false;
        FM225_RingBuf_Reset();
        rx_overflow = false;
        return;
    }

    active_command = FM225_CMD_NONE;
    active_cmd_deadline = 0;
    enrolling = false;
    FM225_RingBuf_Reset();
    rx_overflow = false;

    if (cmd == FM225_CMD_ENROLL && cb_enroll_fail != NULL)
    {
        cb_enroll_fail(FM225_ERR_TIMEOUT);
    }
    else if (cmd == FM225_CMD_VERIFY && cb_invalid != NULL)
    {
        cb_invalid(FM225_ERR_TIMEOUT);
    }

    if (cmd != FM225_CMD_RESET)
    {
        FM225_SendCommand(FM225_CMD_RESET, NULL, 0);
    }
}

static void FM225_SendCommand(FM225_Command_t cmd, const uint8_t *data, uint16_t size)
{
    uint8_t header[5];
    uint8_t checksum = 0;
    uint16_t i;

    if (active_command != FM225_CMD_NONE)
    {
        return;
    }

    FM225_RingBuf_Reset();
    rx_overflow = false;
    uart_error_pending = false;

    active_command = cmd;
    active_cmd_deadline = HAL_GetTick() + FM225_GetCommandTimeoutMs(cmd);

    header[0] = (uint8_t)(FM225_START_CODE >> 8);
    header[1] = (uint8_t)(FM225_START_CODE & 0xFF);
    header[2] = (uint8_t)cmd;
    header[3] = (uint8_t)(size >> 8);
    header[4] = (uint8_t)(size & 0xFF);

    checksum ^= header[2];
    checksum ^= header[3];
    checksum ^= header[4];

    if (HAL_UART_Transmit(&huart2_fm225, header, 5, 100) != HAL_OK)
    {
        FM225_UART_Recover();
        FM225_HandleCommandTimeout(cmd);
        return;
    }

    if (data != NULL && size > 0)
    {
        for (i = 0; i < size; i++)
        {
            checksum ^= data[i];
        }
        if (HAL_UART_Transmit(&huart2_fm225, (uint8_t *)data, size, 200) != HAL_OK)
        {
            FM225_UART_Recover();
            FM225_HandleCommandTimeout(cmd);
            return;
        }
    }

    if (HAL_UART_Transmit(&huart2_fm225, &checksum, 1, 100) != HAL_OK)
    {
        FM225_UART_Recover();
        FM225_HandleCommandTimeout(cmd);
        return;
    }
}

/* ==================== 接收与解析 ==================== */

static void FM225_RecvCommand(void)
{
    uint8_t byte, checksum = 0;
    uint16_t length = 0;
    uint16_t idx;
    FM225_ResponseType_t resp_type;

    if (FM225_RingBuf_Available() < 7)
    {
        return;
    }

    /* 帧头校验 */
    if (!FM225_RingBuf_Pop(&byte) || byte != (uint8_t)(FM225_START_CODE >> 8))
        return;
    if (!FM225_RingBuf_Pop(&byte) || byte != (uint8_t)(FM225_START_CODE & 0xFF))
        return;

    /* 响应类型 */
    FM225_RingBuf_Pop(&byte);
    checksum ^= byte;
    resp_type = (FM225_ResponseType_t)byte;

    /* 数据长度 */
    FM225_RingBuf_Pop(&byte);
    checksum ^= byte;
    length = (uint16_t)byte << 8;
    FM225_RingBuf_Pop(&byte);
    checksum ^= byte;
    length |= byte;

    if (length > FM225_MAX_RESPONSE_SIZE)
    {
        /* 帧太长，丢弃数据段 + 校验字节 */
        for (idx = 0; idx < length + 1; idx++)
        {
            if (!FM225_RingBuf_Pop(&byte))
                break;
        }
        /* 帧异常，触发恢复流程 */
        FM225_HandleCommandTimeout(active_command);
        return;
    }

    /* 清空接收缓冲区，防止残留数据干扰 */
    memset(recv_buf, 0, sizeof(recv_buf));

    /* 读取数据段 */
    for (idx = 0; idx < length; idx++)
    {
        if (!FM225_RingBuf_Pop(&byte))
            return;
        checksum ^= byte;
        recv_buf[idx] = byte;
    }

    /* 校验 */
    if (!FM225_RingBuf_Pop(&byte))
        return;
    if (byte != checksum)
        return;

    /* 分发处理 */
    switch (resp_type)
    {
    case FM225_RESP_NOTE:
        FM225_HandleNote(recv_buf, length);
        break;
    case FM225_RESP_REPLY:
        FM225_HandleReply(recv_buf, length);
        break;
    default:
        break;
    }
}

/* ==================== 处理通知帧 ==================== */

static void FM225_HandleNote(const uint8_t *data, uint16_t length)
{
    if (length < 1)
        return;

    switch (data[0])
    {
    case FM225_NOTE_FACE_STATE:
        if (length >= 17 && cb_face_info != NULL)
        {
            int16_t info[8];
            uint8_t offset = 1;
            uint8_t i;
            for (i = 0; i < 8; i++)
            {
                info[i] = (int16_t)((uint16_t)data[offset + 1] << 8) | data[offset];
                offset += 2;
            }
            cb_face_info(info[0], info[1], info[2], info[3],
                         info[4], info[5], info[6], info[7]);
        }
        break;

    case FM225_NOTE_READY:
    {
        FM225_Command_t timedout_cmd = active_command;
        /* 命令超时通知 */
        switch (timedout_cmd)
        {
        case FM225_CMD_ENROLL:
            enrolling = false;
            if (cb_enroll_fail)
                cb_enroll_fail(FM225_ERR_TIMEOUT);
            break;
        case FM225_CMD_VERIFY:
            if (cb_invalid)
                cb_invalid(FM225_ERR_TIMEOUT);
            break;
        default:
            break;
        }
        active_command = FM225_CMD_NONE;
        active_cmd_deadline = 0;
        break;
    }

    default:
        break;
    }
}

/* ==================== 处理应答帧 ==================== */

static void FM225_HandleReply(const uint8_t *data, uint16_t length)
{
    FM225_Command_t expected = active_command;

    /* 缓存原始响应数据用于诊断 */
    last_reply_len = (length > 16) ? 16 : length;
    memcpy(last_reply, data, last_reply_len);

    if (length < 2)
        return;

    /* 如果收到的指令码与期待的不符，说明这是历史遗留的回复（例如很晚才回的 GET_ALL_IDS），
     * 直接丢弃，保留当前的 active_command 等待真正的回复。 */
    if (data[0] != (uint8_t)expected)
        return;

    /* 匹配成功，清除活动命令 */
    active_command = FM225_CMD_NONE;
    active_cmd_deadline = 0;

    /* 命令执行失败 */
    if (data[1] != FM225_OK)
    {
        switch (expected)
        {
        case FM225_CMD_ENROLL:
            enrolling = false;
            if (cb_enroll_fail)
                cb_enroll_fail(data[1]);
            break;
        case FM225_CMD_VERIFY:
            if (data[1] == FM225_REJECTED)
            {
                if (cb_unmatched)
                    cb_unmatched();
            }
            else
            {
                if (cb_invalid)
                    cb_invalid(data[1]);
            }
            break;
        default:
            break;
        }
        return;
    }

    /* 命令执行成功 */
    switch (expected)
    {
    case FM225_CMD_VERIFY:
    {
        if (length < 4 + FM225_NAME_SIZE)
            break;
        int16_t face_id = ((int16_t)data[2] << 8) | data[3];
        if (cb_matched)
        {
            /* 复制名字并确保结束符 */
            char name[FM225_NAME_SIZE + 1];
            memcpy(name, &data[4], FM225_NAME_SIZE);
            name[FM225_NAME_SIZE] = '\0';
            cb_matched(face_id, name);
        }
        break;
    }
    case FM225_CMD_ENROLL:
    {
        if (length < 5)
            break;
        int16_t face_id = ((int16_t)data[2] << 8) | data[3];
        uint8_t direction = data[4];
        enrolling = false;
        if (cb_enroll_done)
            cb_enroll_done(face_id, direction);
            
        /* 仅当最终录入完成返回有效 ID 时，才自动查询人脸数量更新状态 */
        if (face_id != -1)
        {
            deferred_cmd = FM225_CMD_GET_ALL_IDS;
        }
        break;
    }
    case FM225_CMD_GET_STATUS:
        /* 获取状态后自动查询版本 */
        deferred_cmd = FM225_CMD_GET_VERSION;
        break;
    case FM225_CMD_GET_VERSION:
        /* 获取版本后自动查询人脸数量 */
        deferred_cmd = FM225_CMD_GET_ALL_IDS;
        break;
    case FM225_CMD_GET_ALL_IDS:
        if (cb_face_count && length >= 3)
            cb_face_count(data[2]);
        break;
    case FM225_CMD_DELETE_FACE:
        break;
    case FM225_CMD_DELETE_ALL:
        break;
    case FM225_CMD_RESET:
        /* 复位完成后自动查询状态 */
        deferred_cmd = FM225_CMD_GET_STATUS;
        break;
    default:
        break;
    }
}

/* ==================== 公开 API ==================== */

void FM225_Init(void)
{
    FM225_UART_Init();
    enrolling = false;
    active_command = FM225_CMD_NONE;
    active_cmd_deadline = 0;
    deferred_cmd = FM225_CMD_NONE;
    uart_error_pending = false;
    rx_overflow = false;
    rx_last_tick = HAL_GetTick();

    /* 上电后自动查询模块状态 */
    HAL_Delay(200);
    FM225_SendCommand(FM225_CMD_GET_STATUS, NULL, 0);
}

void FM225_Process(void)
{
    if (uart_error_pending)
    {
        uart_error_pending = false;
        FM225_UART_Recover();
        if (active_command != FM225_CMD_NONE)
        {
            FM225_HandleCommandTimeout(active_command);
            return;
        }
    }

    if (rx_overflow)
    {
        rx_overflow = false;
        FM225_UART_Recover();
        FM225_RingBuf_Reset();
        if (active_command != FM225_CMD_NONE)
        {
            FM225_HandleCommandTimeout(active_command);
            return;
        }
    }

    if (FM225_RingBuf_Available() > 0 &&
        (HAL_GetTick() - rx_last_tick) > FM225_RX_STALE_TIMEOUT_MS)
    {
        FM225_RingBuf_Reset();
    }

    /* 超时检测 */
    if (active_command != FM225_CMD_NONE)
    {
        if (active_cmd_deadline != 0 && FM225_IsExpired(active_cmd_deadline))
        {
            FM225_UART_Recover();
            FM225_HandleCommandTimeout(active_command);
            return;
        }
    }

    /* 解析接收数据 */
    FM225_RecvCommand();

    /* 处理延迟命令 */
    if (deferred_cmd != FM225_CMD_NONE && active_command == FM225_CMD_NONE)
    {
        FM225_Command_t cmd = deferred_cmd;
        deferred_cmd = FM225_CMD_NONE;
        FM225_SendCommand(cmd, NULL, 0);
    }
}

void FM225_EnrollFace(const char *name, FM225_FaceDirection_t direction)
{
    uint8_t data[35];
    uint8_t name_len;

    if (name == NULL)
        return;

    name_len = (uint8_t)strlen(name);
    if (name_len > FM225_NAME_SIZE - 1)
        return;

    memset(data, 0, sizeof(data));
    data[0] = 0; /* admin 标志 */
    memcpy(&data[1], name, name_len);
    /* data[1..32] 名字区域 (已清零) */
    data[33] = (uint8_t)direction;
    data[34] = 10; /* 超时时间 10s */

    FM225_SendCommand(FM225_CMD_ENROLL, data, sizeof(data));
    enrolling = true;
}

void FM225_VerifyFace(void)
{
    static const uint8_t data[2] = {0, 0};
    FM225_SendCommand(FM225_CMD_VERIFY, data, sizeof(data));
}

void FM225_DeleteFace(int16_t face_id)
{
    uint8_t data[2];
    data[0] = (uint8_t)(face_id >> 8);
    data[1] = (uint8_t)(face_id & 0xFF);
    FM225_SendCommand(FM225_CMD_DELETE_FACE, data, sizeof(data));
}

void FM225_DeleteAllFaces(void)
{
    FM225_SendCommand(FM225_CMD_DELETE_ALL, NULL, 0);
}

void FM225_GetFaceCount(void)
{
    FM225_SendCommand(FM225_CMD_GET_ALL_IDS, NULL, 0);
}

void FM225_Reset(void)
{
    active_command = FM225_CMD_NONE;
    active_cmd_deadline = 0;
    enrolling = false;
    uart_error_pending = false;
    FM225_RingBuf_Reset();
    FM225_SendCommand(FM225_CMD_RESET, NULL, 0);
}

bool FM225_IsEnrolling(void)
{
    return enrolling;
}

FM225_Command_t FM225_GetActiveCommand(void)
{
    return active_command;
}

uint16_t FM225_GetLastReply(uint8_t *buf, uint16_t max_len)
{
    uint16_t copy_len = (last_reply_len < max_len) ? last_reply_len : max_len;
    memcpy(buf, last_reply, copy_len);
    return last_reply_len;
}

/* ==================== 回调注册 ==================== */

void FM225_SetMatchedCallback(FM225_MatchedCb_t cb) { cb_matched = cb; }
void FM225_SetUnmatchedCallback(FM225_UnmatchedCb_t cb) { cb_unmatched = cb; }
void FM225_SetInvalidCallback(FM225_InvalidCb_t cb) { cb_invalid = cb; }
void FM225_SetEnrollDoneCallback(FM225_EnrollDoneCb_t cb) { cb_enroll_done = cb; }
void FM225_SetEnrollFailCallback(FM225_EnrollFailCb_t cb) { cb_enroll_fail = cb; }
void FM225_SetFaceInfoCallback(FM225_FaceInfoCb_t cb) { cb_face_info = cb; }
void FM225_SetFaceCountCallback(FM225_FaceCountCb_t cb) { cb_face_count = cb; }
