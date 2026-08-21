#include ".\Hardware\ESP_01S\ESP_01S.h"
#include <string.h>
#include <stdio.h>

/* ==================== 私有变量 ==================== */

static UART_HandleTypeDef huart1_esp; /* 模块内部 UART 句柄 */

/* 环形接收缓冲区 */
static volatile uint8_t rx_buf[ESP_RX_BUF_SIZE];
static volatile uint16_t rx_head = 0;
static volatile uint16_t rx_tail = 0;

/* 中断单字节接收 */
static uint8_t rx_byte;

/* AT 应答相关 */
static volatile uint8_t at_resp_buf[ESP_RX_BUF_SIZE];
static volatile uint16_t at_resp_len = 0;
static volatile bool at_waiting = false;

/* 用户回调 */
static ESP_RxCallback_t user_callback = NULL;

/* ==================== 私有函数声明 ==================== */

static void ESP_UART_Init(void);
static void ESP_RingBuf_Push(uint8_t byte);
static bool ESP_RingBuf_Pop(uint8_t *byte);
static void ESP_ParseIPD(void);
static void ESP_Delay(uint32_t ms);
static void ESP_UART_Recover(void);

/* ==================== UART1 硬件初始化 ==================== */

static void ESP_UART_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* 使能时钟 */
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* PA9  -> USART1_TX (复用推挽) */
    GPIO_InitStruct.Pin = GPIO_PIN_9;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* PA10 -> USART1_RX (浮空输入) */
    GPIO_InitStruct.Pin = GPIO_PIN_10;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* UART 参数 */
    huart1_esp.Instance = USART1;
    huart1_esp.Init.BaudRate = ESP_UART_BAUD;
    huart1_esp.Init.WordLength = UART_WORDLENGTH_8B;
    huart1_esp.Init.StopBits = UART_STOPBITS_1;
    huart1_esp.Init.Parity = UART_PARITY_NONE;
    huart1_esp.Init.Mode = UART_MODE_TX_RX;
    huart1_esp.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1_esp.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart1_esp) != HAL_OK)
    {
        return;
    }

    /* 使能 NVIC 中断 */
    HAL_NVIC_SetPriority(USART1_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);

    /* 启动中断接收（单字节） */
    rx_head = 0;
    rx_tail = 0;
    at_resp_len = 0;
    at_waiting = false;
    HAL_UART_Receive_IT(&huart1_esp, &rx_byte, 1);
}

/* ==================== 中断服务函数 ==================== */

/**
 * @brief USART1 中断入口 —— 放在本模块内，
 *        如果工程其他地方已有此函数需删除或用 __weak 处理
 */
void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart1_esp);
}

void ESP_RxCallback(void)
{
    /* 写入环形缓冲区 */
    ESP_RingBuf_Push(rx_byte);

    /* AT 等待模式下同时写入应答缓冲区 */
    if (at_waiting && at_resp_len < ESP_RX_BUF_SIZE - 1)
    {
        at_resp_buf[at_resp_len++] = rx_byte;
        at_resp_buf[at_resp_len] = '\0';
    }

    /* 重新启动下一字节接收 */
    HAL_UART_Receive_IT(&huart1_esp, &rx_byte, 1);
}

void ESP_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart == NULL || huart->Instance != USART1)
    {
        return;
    }

    ESP_UART_Recover();
}


/* ==================== 环形缓冲区 ==================== */

static void ESP_RingBuf_Push(uint8_t byte)
{
    uint16_t next = (rx_head + 1) % ESP_RX_BUF_SIZE;
    if (next != rx_tail) /* 满则丢弃 */
    {
        rx_buf[rx_head] = byte;
        rx_head = next;
    }
}

static bool ESP_RingBuf_Pop(uint8_t *byte)
{
    if (rx_tail == rx_head)
        return false;
    *byte = rx_buf[rx_tail];
    rx_tail = (rx_tail + 1) % ESP_RX_BUF_SIZE;
    return true;
}

static void ESP_UART_Recover(void)
{
    __HAL_UART_DISABLE_IT(&huart1_esp, UART_IT_RXNE);
    __HAL_UART_DISABLE_IT(&huart1_esp, UART_IT_PE);
    __HAL_UART_DISABLE_IT(&huart1_esp, UART_IT_ERR);

    __HAL_UART_CLEAR_PEFLAG(&huart1_esp);
    __HAL_UART_CLEAR_FEFLAG(&huart1_esp);
    __HAL_UART_CLEAR_NEFLAG(&huart1_esp);
    __HAL_UART_CLEAR_OREFLAG(&huart1_esp);

    rx_head = 0;
    rx_tail = 0;
    at_resp_len = 0;
    at_waiting = false;
    huart1_esp.ErrorCode = HAL_UART_ERROR_NONE;
    huart1_esp.RxState = HAL_UART_STATE_READY;
    (void)HAL_UART_Receive_IT(&huart1_esp, &rx_byte, 1);
}

/* ==================== AT 指令收发 ==================== */

ESP_Status_t ESP_SendAT(const char *cmd, const char *ack, uint32_t timeout)
{
    /* 清空应答缓冲 */
    at_resp_len = 0;
    memset((void *)at_resp_buf, 0, ESP_RX_BUF_SIZE);
    at_waiting = true;

    /* 发送指令 */
    HAL_UART_Transmit(&huart1_esp, (uint8_t *)cmd, strlen(cmd), 1000);
    HAL_UART_Transmit(&huart1_esp, (uint8_t *)"\r\n", 2, 100);

    /* 等待应答 */
    uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start) < timeout)
    {
        if (strstr((const char *)at_resp_buf, ack) != NULL)
        {
            at_waiting = false;
            return ESP_OK;
        }
        if (strstr((const char *)at_resp_buf, "ERROR") != NULL)
        {
            at_waiting = false;
            return ESP_ERR_AT_FAIL;
        }
        ESP_Delay(10);
    }

    at_waiting = false;
    return ESP_ERR_TIMEOUT;
}

/* ==================== 模块初始化 ==================== */

ESP_Status_t ESP_Init(void)
{
    char cmd_buf[128];

    /* 1. 初始化 UART1 硬件 */
    ESP_UART_Init();
    ESP_Delay(1000); /* 等待 ESP-01S 上电就绪 */

    /* 2. 测试 AT */
    if (ESP_SendAT("AT", "OK", ESP_AT_TIMEOUT) != ESP_OK)
    {
        /* 可能波特率不对，尝试复位 */
        ESP_SendAT("AT+RST", "ready", 5000);
        ESP_Delay(2000);
        if (ESP_SendAT("AT", "OK", ESP_AT_TIMEOUT) != ESP_OK)
            return ESP_ERR_AT_FAIL;
    }

    /* 3. 关闭回显 */
    ESP_SendAT("ATE0", "OK", ESP_AT_TIMEOUT);

    /* 4. 设置 AP 模式 (模式2=AP, 模式3=AP+STA) */
    if (ESP_SendAT("AT+CWMODE=2", "OK", ESP_AT_TIMEOUT) != ESP_OK)
        return ESP_ERR_AT_FAIL;

    /* 5. 配置 AP 参数 */
    snprintf(cmd_buf, sizeof(cmd_buf),
             "AT+CWSAP=\"%s\",\"%s\",1,4",
             ESP_WIFI_SSID, ESP_WIFI_PASS);
    if (ESP_SendAT(cmd_buf, "OK", ESP_AT_TIMEOUT) != ESP_OK)
        return ESP_ERR_AT_FAIL;

    /* 6. 允许多连接 */
    if (ESP_SendAT("AT+CIPMUX=1", "OK", ESP_AT_TIMEOUT) != ESP_OK)
        return ESP_ERR_AT_FAIL;

    /* 7. 开启 TCP 服务器 */
    snprintf(cmd_buf, sizeof(cmd_buf),
             "AT+CIPSERVER=1,%s", ESP_SERVER_PORT);
    if (ESP_SendAT(cmd_buf, "OK", ESP_AT_TIMEOUT) != ESP_OK)
        return ESP_ERR_AT_FAIL;

    /* 8. 设置服务器超时 (180s) */
    ESP_SendAT("AT+CIPSTO=180", "OK", ESP_AT_TIMEOUT);

    return ESP_OK;
}

/* ==================== 数据发送 ==================== */

ESP_Status_t ESP_SendData(uint8_t link_id, const uint8_t *data, uint16_t len)
{
    char cmd_buf[64];

    if (len == 0 || len > ESP_TX_BUF_SIZE)
        return ESP_ERR_SEND_FAIL;

    /* AT+CIPSEND=<link_id>,<length> */
    snprintf(cmd_buf, sizeof(cmd_buf), "AT+CIPSEND=%d,%d", link_id, len);
    if (ESP_SendAT(cmd_buf, ">", 2000) != ESP_OK)
        return ESP_ERR_SEND_FAIL;

    /* 发送实际数据 */
    at_resp_len = 0;
    memset((void *)at_resp_buf, 0, ESP_RX_BUF_SIZE);
    at_waiting = true;

    HAL_UART_Transmit(&huart1_esp, (uint8_t *)data, len, 2000);

    /* 等待 SEND OK */
    uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start) < 3000)
    {
        if (strstr((const char *)at_resp_buf, "SEND OK") != NULL)
        {
            at_waiting = false;
            return ESP_OK;
        }
        if (strstr((const char *)at_resp_buf, "SEND FAIL") != NULL)
        {
            at_waiting = false;
            return ESP_ERR_SEND_FAIL;
        }
        ESP_Delay(5);
    }

    at_waiting = false;
    return ESP_ERR_TIMEOUT;
}

ESP_Status_t ESP_SendString(uint8_t link_id, const char *str)
{
    return ESP_SendData(link_id, (const uint8_t *)str, strlen(str));
}

/* ==================== +IPD 数据解析 ==================== */

/*
 * ESP8266 多连接模式下收到数据格式:
 *   +IPD,<link_id>,<len>:<payload>
 *
 * 解析状态机：在环形缓冲区中查找并提取
 */

typedef enum
{
    PARSE_IDLE = 0,
    PARSE_PLUS,    /* 收到 '+' */
    PARSE_HEADER,  /* 正在匹配 "IPD," */
    PARSE_LINK_ID, /* 解析 link_id */
    PARSE_LENGTH,  /* 解析 length */
    PARSE_DATA     /* 接收净数据 */
} ParseState_t;

static ParseState_t parse_state = PARSE_IDLE;
static char hdr_buf[4];
static uint8_t hdr_idx;
static uint8_t parse_link_id;
static uint16_t parse_data_len;
static uint16_t parse_data_cnt;
static ESP_DataPacket_t parse_packet;

static void ESP_ParseIPD(void)
{
    uint8_t ch;

    while (ESP_RingBuf_Pop(&ch))
    {
        /* AT 等待期间不做 IPD 解析，数据已存入 at_resp_buf */
        if (at_waiting)
            continue;

        switch (parse_state)
        {
        case PARSE_IDLE:
            if (ch == '+')
            {
                parse_state = PARSE_PLUS;
                hdr_idx = 0;
            }
            break;

        case PARSE_PLUS:
            hdr_buf[hdr_idx++] = ch;
            if (hdr_idx == 4)
            {
                if (memcmp(hdr_buf, "IPD,", 4) == 0)
                {
                    parse_state = PARSE_LINK_ID;
                    parse_link_id = 0;
                }
                else
                {
                    parse_state = PARSE_IDLE;
                }
            }
            break;

        case PARSE_LINK_ID:
            if (ch == ',')
            {
                parse_state = PARSE_LENGTH;
                parse_data_len = 0;
            }
            else if (ch >= '0' && ch <= '9')
            {
                parse_link_id = parse_link_id * 10 + (ch - '0');
            }
            else
            {
                parse_state = PARSE_IDLE;
            }
            break;

        case PARSE_LENGTH:
            if (ch == ':')
            {
                parse_state = PARSE_DATA;
                parse_data_cnt = 0;
                if (parse_data_len > ESP_DATA_BUF_SIZE)
                    parse_data_len = ESP_DATA_BUF_SIZE;
            }
            else if (ch >= '0' && ch <= '9')
            {
                parse_data_len = parse_data_len * 10 + (ch - '0');
            }
            else
            {
                parse_state = PARSE_IDLE;
            }
            break;

        case PARSE_DATA:
            if (parse_data_cnt < parse_data_len)
            {
                parse_packet.data[parse_data_cnt++] = ch;
            }
            if (parse_data_cnt >= parse_data_len)
            {
                parse_packet.len = parse_data_len;
                parse_packet.link_id = parse_link_id;
                /* 触发用户回调 */
                if (user_callback != NULL)
                {
                    user_callback(&parse_packet);
                }
                parse_state = PARSE_IDLE;
            }
            break;

        default:
            parse_state = PARSE_IDLE;
            break;
        }
    }
}

/* ==================== 主循环处理 ==================== */

void ESP_Process(void)
{
    ESP_ParseIPD();
}

/* ==================== 回调注册 ==================== */

void ESP_RegisterCallback(ESP_RxCallback_t cb)
{
    user_callback = cb;
}

/* ==================== 工具函数 ==================== */

static void ESP_Delay(uint32_t ms)
{
    HAL_Delay(ms);
}
