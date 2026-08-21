#include "HARDWARE/FM225/FM225.h"

static UART_HandleTypeDef huart_fm225;

static volatile uint8_t rx_buf[FM225_RX_BUF_SIZE];
static volatile uint16_t rx_head = 0U;
static volatile uint16_t rx_tail = 0U;
static uint8_t rx_byte;
static uint8_t recv_buf[FM225_MAX_RESPONSE_SIZE];

static FM225_Command_t active_command = FM225_CMD_NONE;
static uint32_t active_cmd_deadline = 0U;
static bool enrolling = false;
static volatile bool uart_error_pending = false;
static volatile bool rx_overflow = false;
static uint32_t rx_last_tick = 0U;

static FM225_Command_t deferred_cmd = FM225_CMD_NONE;
static uint8_t last_reply[16];
static uint16_t last_reply_len = 0U;

static FM225_MatchedCb_t cb_matched = NULL;
static FM225_UnmatchedCb_t cb_unmatched = NULL;
static FM225_InvalidCb_t cb_invalid = NULL;
static FM225_EnrollDoneCb_t cb_enroll_done = NULL;
static FM225_EnrollFailCb_t cb_enroll_fail = NULL;
static FM225_FaceInfoCb_t cb_face_info = NULL;
static FM225_FaceCountCb_t cb_face_count = NULL;

static void FM225_UART_Init(void);
static void FM225_RingBuf_Push(uint8_t byte);
static bool FM225_RingBuf_Pop(uint8_t *byte);
static bool FM225_RingBuf_Peek(uint16_t offset, uint8_t *byte);
static void FM225_RingBuf_Discard(uint16_t count);
static uint16_t FM225_RingBuf_Available(void);
static void FM225_RingBuf_Reset(void);
static void FM225_SendCommand(FM225_Command_t cmd, const uint8_t *data, uint16_t size);
static void FM225_RecvCommand(void);
static void FM225_HandleNote(const uint8_t *data, uint16_t length);
static void FM225_HandleReply(const uint8_t *data, uint16_t length);
static void FM225_UART_Recover(void);
static uint32_t FM225_GetCommandTimeoutMs(FM225_Command_t cmd);
static bool FM225_IsExpired(uint32_t deadline);
static void FM225_HandleCommandTimeout(FM225_Command_t cmd);

static void FM225_UART_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    FM225_UART_CLK_ENABLE();
    FM225_TX_GPIO_CLK_ENABLE();
    FM225_RX_GPIO_CLK_ENABLE();

    GPIO_InitStruct.Pin = FM225_TX_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = FM225_UART_GPIO_AF;
    HAL_GPIO_Init(FM225_TX_GPIO_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = FM225_RX_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = FM225_UART_GPIO_AF;
    HAL_GPIO_Init(FM225_RX_GPIO_PORT, &GPIO_InitStruct);

    huart_fm225.Instance = FM225_UART;
    huart_fm225.Init.BaudRate = FM225_UART_BAUD;
    huart_fm225.Init.WordLength = UART_WORDLENGTH_8B;
    huart_fm225.Init.StopBits = UART_STOPBITS_1;
    huart_fm225.Init.Parity = UART_PARITY_NONE;
    huart_fm225.Init.Mode = UART_MODE_TX_RX;
    huart_fm225.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart_fm225.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart_fm225) != HAL_OK)
    {
        return;
    }

    HAL_NVIC_SetPriority(FM225_UART_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(FM225_UART_IRQn);

    FM225_RingBuf_Reset();
    uart_error_pending = false;
    rx_overflow = false;
    rx_last_tick = HAL_GetTick();
    (void)HAL_UART_Receive_IT(&huart_fm225, &rx_byte, 1);
}

UART_HandleTypeDef *FM225_GetUartHandle(void)
{
    return &huart_fm225;
}

void FM225_RxCallback(UART_HandleTypeDef *huart)
{
    if ((huart == NULL) || (huart->Instance != FM225_UART))
    {
        return;
    }

    FM225_RingBuf_Push(rx_byte);
    rx_last_tick = HAL_GetTick();
    (void)HAL_UART_Receive_IT(&huart_fm225, &rx_byte, 1);
}

void FM225_ErrorCallback(UART_HandleTypeDef *huart)
{
    if ((huart == NULL) || (huart->Instance != FM225_UART))
    {
        return;
    }

    uart_error_pending = true;
}

static void FM225_RingBuf_Push(uint8_t byte)
{
    uint16_t next = (uint16_t)((rx_head + 1U) % FM225_RX_BUF_SIZE);

    if (next != rx_tail)
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
    {
        return false;
    }

    *byte = rx_buf[rx_tail];
    rx_tail = (uint16_t)((rx_tail + 1U) % FM225_RX_BUF_SIZE);
    return true;
}

static bool FM225_RingBuf_Peek(uint16_t offset, uint8_t *byte)
{
    if (offset >= FM225_RingBuf_Available())
    {
        return false;
    }

    *byte = rx_buf[(rx_tail + offset) % FM225_RX_BUF_SIZE];
    return true;
}

static void FM225_RingBuf_Discard(uint16_t count)
{
    while (count-- > 0U)
    {
        if (rx_tail == rx_head)
        {
            break;
        }

        rx_tail = (uint16_t)((rx_tail + 1U) % FM225_RX_BUF_SIZE);
    }
}

static uint16_t FM225_RingBuf_Available(void)
{
    return (uint16_t)((rx_head + FM225_RX_BUF_SIZE - rx_tail) % FM225_RX_BUF_SIZE);
}

static void FM225_RingBuf_Reset(void)
{
    rx_head = 0U;
    rx_tail = 0U;
}

static uint32_t FM225_GetCommandTimeoutMs(FM225_Command_t cmd)
{
    if (cmd == FM225_CMD_RESET)
    {
        return FM225_RESET_TIMEOUT_MS;
    }

    if (cmd == FM225_CMD_ENROLL || cmd == FM225_CMD_ENROLL_SINGLE)
    {
        return FM225_ENROLL_TIMEOUT_MS;
    }

    return FM225_CMD_TIMEOUT_MS;
}

static bool FM225_IsExpired(uint32_t deadline)
{
    return ((int32_t)(HAL_GetTick() - deadline) >= 0);
}

static void FM225_UART_Recover(void)
{
    __HAL_UART_DISABLE_IT(&huart_fm225, UART_IT_RXNE);
    __HAL_UART_DISABLE_IT(&huart_fm225, UART_IT_PE);
    __HAL_UART_DISABLE_IT(&huart_fm225, UART_IT_ERR);

    __HAL_UART_CLEAR_PEFLAG(&huart_fm225);
    __HAL_UART_CLEAR_FEFLAG(&huart_fm225);
    __HAL_UART_CLEAR_NEFLAG(&huart_fm225);
    __HAL_UART_CLEAR_OREFLAG(&huart_fm225);

    huart_fm225.ErrorCode = HAL_UART_ERROR_NONE;
    huart_fm225.RxState = HAL_UART_STATE_READY;
    (void)HAL_UART_Receive_IT(&huart_fm225, &rx_byte, 1);
}

static void FM225_HandleCommandTimeout(FM225_Command_t cmd)
{
    if (cmd == FM225_CMD_NONE)
    {
        active_cmd_deadline = 0U;
        enrolling = false;
        FM225_RingBuf_Reset();
        rx_overflow = false;
        return;
    }

    active_command = FM225_CMD_NONE;
    active_cmd_deadline = 0U;
    enrolling = false;
    FM225_RingBuf_Reset();
    rx_overflow = false;

    if ((cmd == FM225_CMD_ENROLL || cmd == FM225_CMD_ENROLL_SINGLE) && cb_enroll_fail != NULL)
    {
        cb_enroll_fail(FM225_ERR_TIMEOUT);
    }
    else if (cmd == FM225_CMD_VERIFY && cb_invalid != NULL)
    {
        cb_invalid(FM225_ERR_TIMEOUT);
    }
}

static void FM225_SendCommand(FM225_Command_t cmd, const uint8_t *data, uint16_t size)
{
    uint8_t header[5];
    uint8_t checksum = 0U;
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

    if (HAL_UART_Transmit(&huart_fm225, header, 5U, 100U) != HAL_OK)
    {
        FM225_UART_Recover();
        FM225_HandleCommandTimeout(cmd);
        return;
    }

    if ((data != NULL) && (size > 0U))
    {
        for (i = 0U; i < size; i++)
        {
            checksum ^= data[i];
        }

        if (HAL_UART_Transmit(&huart_fm225, (uint8_t *)data, size, 200U) != HAL_OK)
        {
            FM225_UART_Recover();
            FM225_HandleCommandTimeout(cmd);
            return;
        }
    }

    if (HAL_UART_Transmit(&huart_fm225, &checksum, 1U, 100U) != HAL_OK)
    {
        FM225_UART_Recover();
        FM225_HandleCommandTimeout(cmd);
    }
}

static void FM225_RecvCommand(void)
{
    uint8_t byte;
    uint8_t checksum = 0U;
    uint8_t start_hi;
    uint8_t start_lo;
    uint16_t available;
    uint16_t length;
    uint32_t frame_size;
    uint16_t stored_len;
    uint16_t idx;
    FM225_ResponseType_t resp_type;

    available = FM225_RingBuf_Available();
    if (available < 5U)
    {
        return;
    }

    if (!FM225_RingBuf_Peek(0U, &start_hi) || !FM225_RingBuf_Peek(1U, &start_lo))
    {
        return;
    }

    if (start_hi != (uint8_t)(FM225_START_CODE >> 8) ||
        start_lo != (uint8_t)(FM225_START_CODE & 0xFF))
    {
        FM225_RingBuf_Discard(1U);
        return;
    }

    if (!FM225_RingBuf_Peek(2U, &byte))
    {
        return;
    }
    resp_type = (FM225_ResponseType_t)byte;
    checksum ^= byte;

    if (!FM225_RingBuf_Peek(3U, &byte))
    {
        return;
    }
    checksum ^= byte;
    length = (uint16_t)byte << 8;

    if (!FM225_RingBuf_Peek(4U, &byte))
    {
        return;
    }
    checksum ^= byte;
    length |= byte;

    frame_size = 6U + (uint32_t)length;
    if (frame_size > (uint32_t)(FM225_RX_BUF_SIZE - 1U))
    {
        FM225_RingBuf_Discard(1U);
        return;
    }

    if ((uint32_t)available < frame_size)
    {
        return;
    }

    FM225_RingBuf_Discard(5U);
    memset(recv_buf, 0, sizeof(recv_buf));
    stored_len = (length > FM225_MAX_RESPONSE_SIZE) ? FM225_MAX_RESPONSE_SIZE : length;

    for (idx = 0U; idx < length; idx++)
    {
        if (!FM225_RingBuf_Pop(&byte))
        {
            return;
        }

        checksum ^= byte;
        if (idx < stored_len)
        {
            recv_buf[idx] = byte;
        }
    }

    if (!FM225_RingBuf_Pop(&byte))
    {
        return;
    }

    if (byte != checksum)
    {
        return;
    }

    switch (resp_type)
    {
    case FM225_RESP_NOTE:
        FM225_HandleNote(recv_buf, stored_len);
        break;

    case FM225_RESP_REPLY:
        FM225_HandleReply(recv_buf, stored_len);
        break;

    default:
        break;
    }
}

static void FM225_HandleNote(const uint8_t *data, uint16_t length)
{
    if (length < 1U)
    {
        return;
    }

    switch (data[0])
    {
    case FM225_NOTE_FACE_STATE:
        if ((length >= 17U) && (cb_face_info != NULL))
        {
            int16_t info[8];
            uint8_t offset = 1U;
            uint8_t i;

            for (i = 0U; i < 8U; i++)
            {
                info[i] = (int16_t)(((uint16_t)data[offset] << 8) | data[offset + 1U]);
                offset = (uint8_t)(offset + 2U);
            }

            cb_face_info(info[0], info[1], info[2], info[3],
                         info[4], info[5], info[6], info[7]);
        }
        break;

    case FM225_NOTE_READY:
        if (active_command == FM225_CMD_RESET ||
            active_command == FM225_CMD_GET_STATUS ||
            active_command == FM225_CMD_FACE_RESET)
        {
            active_command = FM225_CMD_NONE;
            active_cmd_deadline = 0U;
        }
        else if (active_command != FM225_CMD_NONE)
        {
            FM225_HandleCommandTimeout(active_command);
        }
        break;

    default:
        break;
    }
}

static void FM225_HandleReply(const uint8_t *data, uint16_t length)
{
    FM225_Command_t expected = active_command;

    last_reply_len = (length > sizeof(last_reply)) ? (uint16_t)sizeof(last_reply) : length;
    memcpy(last_reply, data, last_reply_len);

    if (length < 2U)
    {
        return;
    }

    if (data[0] != (uint8_t)expected)
    {
        return;
    }

    active_command = FM225_CMD_NONE;
    active_cmd_deadline = 0U;

    if (data[1] != FM225_OK)
    {
        switch (expected)
        {
        case FM225_CMD_ENROLL:
        case FM225_CMD_ENROLL_SINGLE:
            enrolling = false;
            if (cb_enroll_fail != NULL)
            {
                cb_enroll_fail(data[1]);
            }
            break;

        case FM225_CMD_VERIFY:
            if (data[1] == FM225_REJECTED)
            {
                if (cb_unmatched != NULL)
                {
                    cb_unmatched();
                }
            }
            else if (cb_invalid != NULL)
            {
                cb_invalid(data[1]);
            }
            break;

        default:
            break;
        }
        return;
    }

    switch (expected)
    {
    case FM225_CMD_VERIFY:
        if (length >= 4U)
        {
            int16_t face_id = (int16_t)(((uint16_t)data[2] << 8) | data[3]);
            if (cb_matched != NULL)
            {
                char name[FM225_NAME_SIZE + 1U];
                memset(name, 0, sizeof(name));
                if (length >= (uint16_t)(4U + FM225_NAME_SIZE))
                {
                    memcpy(name, &data[4], FM225_NAME_SIZE);
                }
                name[FM225_NAME_SIZE] = '\0';
                cb_matched(face_id, name);
            }
        }
        break;

    case FM225_CMD_ENROLL:
    case FM225_CMD_ENROLL_SINGLE:
        if (length >= 5U)
        {
            int16_t face_id = (int16_t)(((uint16_t)data[2] << 8) | data[3]);
            uint8_t direction = data[4];
            enrolling = false;
            if (cb_enroll_done != NULL)
            {
                cb_enroll_done(face_id, direction);
            }
        }
        break;

    case FM225_CMD_GET_ALL_IDS:
        if ((cb_face_count != NULL) && (length >= 3U))
        {
            cb_face_count(data[2]);
        }
        break;

    case FM225_CMD_DELETE_FACE:
    case FM225_CMD_DELETE_ALL:
    case FM225_CMD_RESET:
    case FM225_CMD_GET_STATUS:
    case FM225_CMD_FACE_RESET:
    default:
        break;
    }
}

void FM225_Init(void)
{
    FM225_UART_Init();
    enrolling = false;
    active_command = FM225_CMD_NONE;
    active_cmd_deadline = 0U;
    deferred_cmd = FM225_CMD_NONE;
    uart_error_pending = false;
    rx_overflow = false;
    rx_last_tick = HAL_GetTick();
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

    if ((FM225_RingBuf_Available() > 0U) &&
        ((HAL_GetTick() - rx_last_tick) > FM225_RX_STALE_TIMEOUT_MS))
    {
        FM225_RingBuf_Reset();
    }

    if ((active_command != FM225_CMD_NONE) &&
        (active_cmd_deadline != 0U) &&
        FM225_IsExpired(active_cmd_deadline))
    {
        FM225_UART_Recover();
        FM225_HandleCommandTimeout(active_command);
        return;
    }

    FM225_RecvCommand();

    if ((deferred_cmd != FM225_CMD_NONE) && (active_command == FM225_CMD_NONE))
    {
        FM225_Command_t cmd = deferred_cmd;
        deferred_cmd = FM225_CMD_NONE;
        FM225_SendCommand(cmd, NULL, 0U);
    }
}

void FM225_EnrollFace(const char *name, FM225_FaceDirection_t direction)
{
    uint8_t data[35];
    uint8_t name_len;

    if (name == NULL)
    {
        return;
    }

    name_len = (uint8_t)strlen(name);
    if (name_len > FM225_NAME_SIZE)
    {
        return;
    }

    memset(data, 0, sizeof(data));
    data[0] = 0U;
    memcpy(&data[1], name, name_len);

    if (direction == FM225_DIR_UNDEFINED)
    {
        FM225_SendCommand(FM225_CMD_ENROLL_SINGLE, data, sizeof(data));
    }
    else
    {
        data[33] = (uint8_t)direction;
        data[34] = FM225_ENROLL_CAPTURE_TIMEOUT_S;
        FM225_SendCommand(FM225_CMD_ENROLL, data, sizeof(data));
    }
    enrolling = true;
}

void FM225_VerifyFace(void)
{
    static const uint8_t data[2] = {0U, FM225_VERIFY_TIMEOUT_S};
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
    FM225_SendCommand(FM225_CMD_DELETE_ALL, NULL, 0U);
}

void FM225_GetFaceCount(void)
{
    FM225_SendCommand(FM225_CMD_GET_ALL_IDS, NULL, 0U);
}

void FM225_Reset(void)
{
    active_command = FM225_CMD_NONE;
    active_cmd_deadline = 0U;
    enrolling = false;
    uart_error_pending = false;
    FM225_RingBuf_Reset();
    FM225_SendCommand(FM225_CMD_RESET, NULL, 0U);
}

void FM225_Abort(void)
{
    active_command = FM225_CMD_NONE;
    active_cmd_deadline = 0U;
    enrolling = false;
    deferred_cmd = FM225_CMD_NONE;
    uart_error_pending = false;
    rx_overflow = false;
    FM225_RingBuf_Reset();
    FM225_SendCommand(FM225_CMD_RESET, NULL, 0U);
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

void FM225_SetMatchedCallback(FM225_MatchedCb_t cb)
{
    cb_matched = cb;
}

void FM225_SetUnmatchedCallback(FM225_UnmatchedCb_t cb)
{
    cb_unmatched = cb;
}

void FM225_SetInvalidCallback(FM225_InvalidCb_t cb)
{
    cb_invalid = cb;
}

void FM225_SetEnrollDoneCallback(FM225_EnrollDoneCb_t cb)
{
    cb_enroll_done = cb;
}

void FM225_SetEnrollFailCallback(FM225_EnrollFailCb_t cb)
{
    cb_enroll_fail = cb;
}

void FM225_SetFaceInfoCallback(FM225_FaceInfoCb_t cb)
{
    cb_face_info = cb;
}

void FM225_SetFaceCountCallback(FM225_FaceCountCb_t cb)
{
    cb_face_count = cb;
}
