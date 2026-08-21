#include ".\\Hardware\\AS608\\AS608.h"
#include <string.h>

/* ==================== 私有常量 ==================== */

#define AS608_HEADER_H 0xEF
#define AS608_HEADER_L 0x01

#define AS608_PACKET_COMMAND 0x01
#define AS608_PACKET_ACK 0x07

#define AS608_MAX_PACKET_PAYLOAD 64U
#define AS608_MAX_PACKET_CONTENT (AS608_MAX_PACKET_PAYLOAD + 2U)

/* ==================== 私有变量 ==================== */

static UART_HandleTypeDef huart3_as608;

uint32_t AS608Addr = 0xFFFFFFFFU;

/* ==================== 私有函数声明 ==================== */

static uint32_t AS608_GetRemain(uint32_t deadline);
static void AS608_FlushRx(void);
static HAL_StatusTypeDef AS608_SendPacket(uint8_t packet_type, const uint8_t *payload, uint16_t payload_len);
static HAL_StatusTypeDef AS608_ReadPacket(uint8_t *packet_type,
                                          uint32_t *packet_addr,
                                          uint8_t *payload,
                                          uint16_t *payload_len,
                                          uint32_t timeout_ms);
static uint8_t AS608_ExecCommand(const uint8_t *cmd, uint16_t cmd_len, uint8_t *ack, uint16_t *ack_len, uint32_t timeout_ms);

/* ==================== 基础工具 ==================== */

static uint32_t AS608_GetRemain(uint32_t deadline)
{
    int32_t remain = (int32_t)(deadline - HAL_GetTick());
    return (remain > 0) ? (uint32_t)remain : 0U;
}

static void AS608_FlushRx(void)
{
    uint8_t byte;
    while (HAL_UART_Receive(&huart3_as608, &byte, 1, 1) == HAL_OK)
    {
    }
}

/* ==================== 硬件初始化 ==================== */

void GZ_StaGPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitStruct.Pin = AS608_WAK_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(AS608_GPIO_PORT, &GPIO_InitStruct);
}

void AS608_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_RCC_USART3_CLK_ENABLE();

    /* PB10 -> USART3_TX */
    GPIO_InitStruct.Pin = AS608_TX_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(AS608_GPIO_PORT, &GPIO_InitStruct);

    /* PB11 -> USART3_RX */
    GPIO_InitStruct.Pin = AS608_RX_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(AS608_GPIO_PORT, &GPIO_InitStruct);

    GZ_StaGPIO_Init();

    huart3_as608.Instance = AS608_UART;
    huart3_as608.Init.BaudRate = AS608_UART_BAUD;
    huart3_as608.Init.WordLength = UART_WORDLENGTH_8B;
    huart3_as608.Init.StopBits = UART_STOPBITS_1;
    huart3_as608.Init.Parity = UART_PARITY_NONE;
    huart3_as608.Init.Mode = UART_MODE_TX_RX;
    huart3_as608.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart3_as608.Init.OverSampling = UART_OVERSAMPLING_16;
    (void)HAL_UART_Init(&huart3_as608);

    AS608_FlushRx();
}

/* ==================== 协议收发 ==================== */

static HAL_StatusTypeDef AS608_SendPacket(uint8_t packet_type, const uint8_t *payload, uint16_t payload_len)
{
    uint8_t frame[9 + AS608_MAX_PACKET_PAYLOAD + 2];
    uint16_t checksum;
    uint16_t frame_len;
    uint16_t length;
    uint16_t i;

    if (payload_len > AS608_MAX_PACKET_PAYLOAD)
    {
        return HAL_ERROR;
    }

    length = payload_len + 2U;
    checksum = (uint16_t)(packet_type + (length >> 8) + (length & 0xFF));
    for (i = 0; i < payload_len; i++)
    {
        checksum = (uint16_t)(checksum + payload[i]);
    }

    frame[0] = AS608_HEADER_H;
    frame[1] = AS608_HEADER_L;
    frame[2] = (uint8_t)(AS608Addr >> 24);
    frame[3] = (uint8_t)(AS608Addr >> 16);
    frame[4] = (uint8_t)(AS608Addr >> 8);
    frame[5] = (uint8_t)(AS608Addr);
    frame[6] = packet_type;
    frame[7] = (uint8_t)(length >> 8);
    frame[8] = (uint8_t)(length);

    if (payload_len > 0U)
    {
        memcpy(&frame[9], payload, payload_len);
    }

    frame[9 + payload_len] = (uint8_t)(checksum >> 8);
    frame[10 + payload_len] = (uint8_t)(checksum);

    frame_len = (uint16_t)(11U + payload_len);
    return HAL_UART_Transmit(&huart3_as608, frame, frame_len, 200);
}

static HAL_StatusTypeDef AS608_ReadPacket(uint8_t *packet_type,
                                          uint32_t *packet_addr,
                                          uint8_t *payload,
                                          uint16_t *payload_len,
                                          uint32_t timeout_ms)
{
    uint8_t head[9];
    uint8_t content[AS608_MAX_PACKET_CONTENT];
    uint8_t byte;
    uint16_t length;
    uint16_t calc_checksum;
    uint16_t rx_checksum;
    uint32_t deadline;
    uint32_t remain;
    uint16_t i;

    if (packet_type == NULL || packet_addr == NULL || payload == NULL || payload_len == NULL)
    {
        return HAL_ERROR;
    }

    deadline = HAL_GetTick() + timeout_ms;

    /* 同步到包头 0xEF01 */
    while (1)
    {
        remain = AS608_GetRemain(deadline);
        if (remain == 0U)
        {
            return HAL_TIMEOUT;
        }

        if (HAL_UART_Receive(&huart3_as608, &byte, 1, (remain > 5U) ? 5U : remain) != HAL_OK)
        {
            continue;
        }

        if (byte != AS608_HEADER_H)
        {
            continue;
        }

        remain = AS608_GetRemain(deadline);
        if (remain == 0U)
        {
            return HAL_TIMEOUT;
        }

        if (HAL_UART_Receive(&huart3_as608, &byte, 1, remain) != HAL_OK)
        {
            return HAL_TIMEOUT;
        }

        if (byte == AS608_HEADER_L)
        {
            head[0] = AS608_HEADER_H;
            head[1] = AS608_HEADER_L;
            break;
        }
    }

    remain = AS608_GetRemain(deadline);
    if (remain == 0U)
    {
        return HAL_TIMEOUT;
    }

    if (HAL_UART_Receive(&huart3_as608, &head[2], 7, remain) != HAL_OK)
    {
        return HAL_TIMEOUT;
    }

    *packet_addr = ((uint32_t)head[2] << 24) |
                   ((uint32_t)head[3] << 16) |
                   ((uint32_t)head[4] << 8) |
                   (uint32_t)head[5];

    *packet_type = head[6];
    length = (uint16_t)((head[7] << 8) | head[8]);
    if (length < 2U || length > AS608_MAX_PACKET_CONTENT)
    {
        return HAL_ERROR;
    }

    remain = AS608_GetRemain(deadline);
    if (remain == 0U)
    {
        return HAL_TIMEOUT;
    }

    if (HAL_UART_Receive(&huart3_as608, content, length, remain) != HAL_OK)
    {
        return HAL_TIMEOUT;
    }

    calc_checksum = (uint16_t)(head[6] + head[7] + head[8]);
    for (i = 0; i < (uint16_t)(length - 2U); i++)
    {
        calc_checksum = (uint16_t)(calc_checksum + content[i]);
    }

    rx_checksum = (uint16_t)((content[length - 2U] << 8) | content[length - 1U]);
    if (calc_checksum != rx_checksum)
    {
        return HAL_ERROR;
    }

    *payload_len = (uint16_t)(length - 2U);
    if (*payload_len > 0U)
    {
        memcpy(payload, content, *payload_len);
    }

    return HAL_OK;
}

static uint8_t AS608_ExecCommand(const uint8_t *cmd, uint16_t cmd_len, uint8_t *ack, uint16_t *ack_len, uint32_t timeout_ms)
{
    uint8_t packet_type;
    uint8_t payload[AS608_MAX_PACKET_PAYLOAD];
    uint16_t payload_len;
    uint32_t packet_addr;

    if (cmd == NULL || cmd_len == 0U || ack == NULL || ack_len == NULL)
    {
        return 0xFF;
    }

    AS608_FlushRx();

    if (AS608_SendPacket(AS608_PACKET_COMMAND, cmd, cmd_len) != HAL_OK)
    {
        return 0xFF;
    }

    if (AS608_ReadPacket(&packet_type, &packet_addr, payload, &payload_len, timeout_ms) != HAL_OK)
    {
        return 0xFF;
    }

    if (packet_type != AS608_PACKET_ACK || payload_len == 0U)
    {
        return 0xFF;
    }

    if (payload_len > AS608_MAX_PACKET_PAYLOAD)
    {
        return 0xFF;
    }

    memcpy(ack, payload, payload_len);
    *ack_len = payload_len;
    return ack[0];
}

/* ==================== 指令接口 ==================== */

uint8_t GZ_GetImage(void)
{
    uint8_t cmd[1] = {0x01};
    uint8_t ack[AS608_MAX_PACKET_PAYLOAD];
    uint16_t ack_len = 0;
    return AS608_ExecCommand(cmd, sizeof(cmd), ack, &ack_len, AS608_CMD_TIMEOUT_MS);
}

uint8_t GZ_GenChar(uint8_t BufferID)
{
    uint8_t cmd[2] = {0x02, BufferID};
    uint8_t ack[AS608_MAX_PACKET_PAYLOAD];
    uint16_t ack_len = 0;
    return AS608_ExecCommand(cmd, sizeof(cmd), ack, &ack_len, AS608_CMD_TIMEOUT_MS);
}

uint8_t GZ_Match(void)
{
    uint8_t cmd[1] = {0x03};
    uint8_t ack[AS608_MAX_PACKET_PAYLOAD];
    uint16_t ack_len = 0;
    return AS608_ExecCommand(cmd, sizeof(cmd), ack, &ack_len, AS608_CMD_TIMEOUT_MS);
}

uint8_t GZ_Search(uint8_t BufferID, uint16_t StartPage, uint16_t PageNum, SearchResult *p)
{
    uint8_t cmd[6] = {
        0x04,
        BufferID,
        (uint8_t)(StartPage >> 8),
        (uint8_t)StartPage,
        (uint8_t)(PageNum >> 8),
        (uint8_t)PageNum};
    uint8_t ack[AS608_MAX_PACKET_PAYLOAD];
    uint16_t ack_len = 0;
    uint8_t ensure;

    ensure = AS608_ExecCommand(cmd, sizeof(cmd), ack, &ack_len, AS608_CMD_TIMEOUT_MS);
    if (ensure == 0x00 && p != NULL && ack_len >= 5U)
    {
        p->pageID = (uint16_t)((ack[1] << 8) | ack[2]);
        p->mathscore = (uint16_t)((ack[3] << 8) | ack[4]);
    }
    return ensure;
}

uint8_t GZ_RegModel(void)
{
    uint8_t cmd[1] = {0x05};
    uint8_t ack[AS608_MAX_PACKET_PAYLOAD];
    uint16_t ack_len = 0;
    return AS608_ExecCommand(cmd, sizeof(cmd), ack, &ack_len, AS608_CMD_TIMEOUT_MS);
}

uint8_t GZ_StoreChar(uint8_t BufferID, uint16_t PageID)
{
    uint8_t cmd[4] = {
        0x06,
        BufferID,
        (uint8_t)(PageID >> 8),
        (uint8_t)PageID};
    uint8_t ack[AS608_MAX_PACKET_PAYLOAD];
    uint16_t ack_len = 0;
    return AS608_ExecCommand(cmd, sizeof(cmd), ack, &ack_len, AS608_CMD_TIMEOUT_MS);
}

uint8_t GZ_DeletChar(uint16_t PageID, uint16_t N)
{
    uint8_t cmd[5] = {
        0x0C,
        (uint8_t)(PageID >> 8),
        (uint8_t)PageID,
        (uint8_t)(N >> 8),
        (uint8_t)N};
    uint8_t ack[AS608_MAX_PACKET_PAYLOAD];
    uint16_t ack_len = 0;
    return AS608_ExecCommand(cmd, sizeof(cmd), ack, &ack_len, AS608_CMD_TIMEOUT_MS);
}

uint8_t GZ_Empty(void)
{
    uint8_t cmd[1] = {0x0D};
    uint8_t ack[AS608_MAX_PACKET_PAYLOAD];
    uint16_t ack_len = 0;
    return AS608_ExecCommand(cmd, sizeof(cmd), ack, &ack_len, AS608_CMD_TIMEOUT_MS);
}

uint8_t GZ_WriteReg(uint8_t RegNum, uint8_t DATA)
{
    uint8_t cmd[3] = {0x0E, RegNum, DATA};
    uint8_t ack[AS608_MAX_PACKET_PAYLOAD];
    uint16_t ack_len = 0;
    return AS608_ExecCommand(cmd, sizeof(cmd), ack, &ack_len, AS608_CMD_TIMEOUT_MS);
}

uint8_t GZ_ReadSysPara(SysPara *p)
{
    uint8_t cmd[1] = {0x0F};
    uint8_t ack[AS608_MAX_PACKET_PAYLOAD];
    uint16_t ack_len = 0;
    uint8_t ensure;

    ensure = AS608_ExecCommand(cmd, sizeof(cmd), ack, &ack_len, AS608_CMD_TIMEOUT_MS);
    if (ensure == 0x00 && p != NULL && ack_len >= 17U)
    {
        p->GZ_max = (uint16_t)((ack[5] << 8) | ack[6]);
        p->GZ_level = ack[8];
        p->GZ_addr = ((uint32_t)ack[9] << 24) |
                     ((uint32_t)ack[10] << 16) |
                     ((uint32_t)ack[11] << 8) |
                     (uint32_t)ack[12];
        p->GZ_size = ack[14];
        p->GZ_N = ack[16];
    }
    return ensure;
}

uint8_t GZ_SetAddr(uint32_t GZ_addr)
{
    uint8_t cmd[5] = {
        0x15,
        (uint8_t)(GZ_addr >> 24),
        (uint8_t)(GZ_addr >> 16),
        (uint8_t)(GZ_addr >> 8),
        (uint8_t)GZ_addr};
    uint8_t ack[AS608_MAX_PACKET_PAYLOAD];
    uint16_t ack_len = 0;
    uint8_t ensure;

    ensure = AS608_ExecCommand(cmd, sizeof(cmd), ack, &ack_len, AS608_CMD_TIMEOUT_MS);
    if (ensure == 0x00)
    {
        AS608Addr = GZ_addr;
    }
    return ensure;
}

uint8_t GZ_WriteNotepad(uint8_t NotePageNum, uint8_t *content)
{
    uint8_t cmd[34];
    uint8_t ack[AS608_MAX_PACKET_PAYLOAD];
    uint16_t ack_len = 0;

    if (content == NULL)
    {
        return 0xFF;
    }

    cmd[0] = 0x18;
    cmd[1] = NotePageNum;
    memcpy(&cmd[2], content, 32);

    return AS608_ExecCommand(cmd, sizeof(cmd), ack, &ack_len, AS608_CMD_TIMEOUT_MS);
}

uint8_t GZ_ReadNotepad(uint8_t NotePageNum, uint8_t *note)
{
    uint8_t cmd[2] = {0x19, NotePageNum};
    uint8_t ack[AS608_MAX_PACKET_PAYLOAD];
    uint16_t ack_len = 0;
    uint8_t ensure;

    ensure = AS608_ExecCommand(cmd, sizeof(cmd), ack, &ack_len, AS608_CMD_TIMEOUT_MS);
    if (ensure == 0x00 && note != NULL && ack_len >= 33U)
    {
        memcpy(note, &ack[1], 32);
    }
    return ensure;
}

uint8_t GZ_HighSpeedSearch(uint8_t BufferID, uint16_t StartPage, uint16_t PageNum, SearchResult *p)
{
    uint8_t cmd[6] = {
        0x1B,
        BufferID,
        (uint8_t)(StartPage >> 8),
        (uint8_t)StartPage,
        (uint8_t)(PageNum >> 8),
        (uint8_t)PageNum};
    uint8_t ack[AS608_MAX_PACKET_PAYLOAD];
    uint16_t ack_len = 0;
    uint8_t ensure;

    ensure = AS608_ExecCommand(cmd, sizeof(cmd), ack, &ack_len, AS608_CMD_TIMEOUT_MS);
    if (ensure == 0x00 && p != NULL && ack_len >= 5U)
    {
        p->pageID = (uint16_t)((ack[1] << 8) | ack[2]);
        p->mathscore = (uint16_t)((ack[3] << 8) | ack[4]);
    }
    return ensure;
}

uint8_t GZ_ValidTempleteNum(uint16_t *ValidN)
{
    uint8_t cmd[1] = {0x1D};
    uint8_t ack[AS608_MAX_PACKET_PAYLOAD];
    uint16_t ack_len = 0;
    uint8_t ensure;

    ensure = AS608_ExecCommand(cmd, sizeof(cmd), ack, &ack_len, AS608_CMD_TIMEOUT_MS);
    if (ensure == 0x00 && ValidN != NULL && ack_len >= 3U)
    {
        *ValidN = (uint16_t)((ack[1] << 8) | ack[2]);
    }
    return ensure;
}

uint8_t GZ_HandShake(uint32_t *GZ_Addr)
{
    uint8_t handshake_frame[9] = {
        AS608_HEADER_H,
        AS608_HEADER_L,
        (uint8_t)(AS608Addr >> 24),
        (uint8_t)(AS608Addr >> 16),
        (uint8_t)(AS608Addr >> 8),
        (uint8_t)AS608Addr,
        AS608_PACKET_COMMAND,
        0x00,
        0x00};

    uint8_t packet_type;
    uint8_t payload[AS608_MAX_PACKET_PAYLOAD];
    uint16_t payload_len;
    uint32_t packet_addr;

    AS608_FlushRx();

    if (HAL_UART_Transmit(&huart3_as608, handshake_frame, sizeof(handshake_frame), 100) != HAL_OK)
    {
        return 1;
    }

    if (AS608_ReadPacket(&packet_type, &packet_addr, payload, &payload_len, AS608_CMD_TIMEOUT_MS) != HAL_OK)
    {
        return 1;
    }

    if (packet_type != AS608_PACKET_ACK)
    {
        return 1;
    }

    if (GZ_Addr != NULL)
    {
        *GZ_Addr = packet_addr;
    }

    AS608Addr = packet_addr;
    return 0;
}

/* ==================== 错误码解析 ==================== */

const char *EnsureMessage(uint8_t ensure)
{
    const char *p;
    switch (ensure)
    {
    case 0x00:
        p = "OK";
        break;
    case 0x01:
        p = "数据包接收错误";
        break;
    case 0x02:
        p = "传感器上没有手指";
        break;
    case 0x03:
        p = "录入指纹图像失败";
        break;
    case 0x04:
        p = "指纹图像太干、太淡而生不成特征";
        break;
    case 0x05:
        p = "指纹图像太湿、太糊而生不成特征";
        break;
    case 0x06:
        p = "指纹图像太乱而生不成特征";
        break;
    case 0x07:
        p = "指纹图像正常，但特征点太少（或面积太小）而生不成特征";
        break;
    case 0x08:
        p = "指纹不匹配";
        break;
    case 0x09:
        p = "没搜索到指纹";
        break;
    case 0x0A:
        p = "特征合并失败";
        break;
    case 0x0B:
        p = "访问指纹库时地址序号超出指纹库范围";
        break;
    case 0x10:
        p = "删除模板失败";
        break;
    case 0x11:
        p = "清空指纹库失败";
        break;
    case 0x15:
        p = "缓冲区内没有有效原始图而生不成图像";
        break;
    case 0x18:
        p = "读写 FLASH 出错";
        break;
    case 0x19:
        p = "未定义错误";
        break;
    case 0x1A:
        p = "无效寄存器号";
        break;
    case 0x1B:
        p = "寄存器设定内容错误";
        break;
    case 0x1C:
        p = "记事本页码指定错误";
        break;
    case 0x1F:
        p = "指纹库满";
        break;
    case 0x20:
        p = "地址错误";
        break;
    case 0xFF:
        p = "通信超时或校验失败";
        break;
    default:
        p = "模块返回确认码有误";
        break;
    }

    return p;
}
