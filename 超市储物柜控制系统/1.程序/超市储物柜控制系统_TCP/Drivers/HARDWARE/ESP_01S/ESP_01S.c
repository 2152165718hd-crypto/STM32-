#include "HARDWARE/ESP_01S/ESP_01S.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ESP_NOTICE_BUF_SIZE 128U
#define ESP_RESET_TIMEOUT 5000U
#define ESP_PARSE_DATA_TIMEOUT_MS 300U

typedef enum
{
    PARSE_TEXT = 0,
    PARSE_PLUS,
    PARSE_I,
    PARSE_P,
    PARSE_D,
    PARSE_HEADER,
    PARSE_DATA
} ESP_ParseState_t;

typedef enum
{
    ESP_CONN_IDLE = 0,
    ESP_CONN_AT,
    ESP_CONN_ATE0,
    ESP_CONN_CWMODE,
    ESP_CONN_CWSAP,
    ESP_CONN_CIPMUX,
    ESP_CONN_CIPDINFO,
    ESP_CONN_CIPSERVER
} ESP_ConnState_t;

typedef enum
{
    ESP_ASYNC_PENDING = 0,
    ESP_ASYNC_OK,
    ESP_ASYNC_FAIL
} ESP_AsyncResult_t;

typedef enum
{
    ESP_SEND_IDLE = 0,
    ESP_SEND_WAIT_PROMPT,
    ESP_SEND_WAIT_RESULT
} ESP_SendState_t;

static UART_HandleTypeDef s_esp_uart;

static volatile uint8_t rx_buf[ESP_RX_BUF_SIZE];
static volatile uint16_t rx_head = 0U;
static volatile uint16_t rx_tail = 0U;
static volatile uint8_t rx_overflow = 0U;
static volatile uint32_t rx_last_tick = 0U;
static uint8_t rx_byte = 0U;

static volatile uint8_t at_resp_buf[ESP_RX_BUF_SIZE];
static volatile uint16_t at_resp_len = 0U;
static volatile bool at_waiting = false;

static ESP_RxCallback_t user_callback = NULL;
static uint8_t esp_initialized = 0U;
static uint8_t wifi_connected = 0U;
static uint8_t tcp_connected = 0U;
static uint8_t server_ready = 0U;
static uint8_t active_link_id = 0xFFU;
static uint32_t last_reconnect_tick = 0U;
static ESP_ConnState_t conn_state = ESP_CONN_IDLE;
static const char *async_ack = NULL;
static uint32_t async_start_tick = 0U;
static uint32_t async_timeout = 0U;
static char async_cmd_buf[128];
static ESP_SendState_t send_state = ESP_SEND_IDLE;
static uint8_t send_buf[ESP_TX_BUF_SIZE];
static uint16_t send_len = 0U;
static uint32_t send_start_tick = 0U;

static ESP_ParseState_t parse_state = PARSE_TEXT;
static char parse_header_buf[24];
static uint8_t parse_header_len = 0U;
static uint8_t parse_link_id = 0xFFU;
static uint16_t parse_data_len = 0U;
static uint16_t parse_data_cnt = 0U;
static uint32_t parse_state_tick = 0U;
static ESP_DataPacket_t parse_packet = {{0}, 0U, 0U};

static char notice_buf[ESP_NOTICE_BUF_SIZE];
static uint16_t notice_len = 0U;

static void ESP_UART_Init(void);
static void ESP_RingBuf_Push(uint8_t byte);
static bool ESP_RingBuf_Pop(uint8_t *byte);
static void ESP_RingBuf_Reset(void);
static void ESP_ClearAtResponse(void);
static void ESP_ResetParseState(void);
static void ESP_HandleNoticeChar(uint8_t ch);
static void ESP_HandleNoticeLine(const char *line);
static void ESP_ParseIncoming(void);
static void ESP_HandleRxOverflow(void);
static void ESP_CheckParseTimeout(void);
static void ESP_Delay(uint32_t ms);
static void ESP_ConnectionAbort(void);
static void ESP_ConnectionAdvance(void);
static void ESP_SetClientConnected(uint8_t link_id);
static void ESP_ClearClientConnected(void);
static uint8_t ESP_ParseLinkId(const char *line, uint8_t *link_id);
static uint8_t ESP_StartAsyncAT(const char *cmd, const char *ack, uint32_t timeout);
static ESP_AsyncResult_t ESP_PollAsyncAT(void);
static void ESP_ProcessConnection(void);
static void ESP_ProcessSend(void);
static void ESP_ResetSendState(void);

static void ESP_UART_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    ESP_UART_CLK_ENABLE();
    ESP_UART_GPIO_CLK_ENABLE();

    GPIO_InitStruct.Pin = ESP_UART_TX_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = ESP_UART_GPIO_AF;
    HAL_GPIO_Init(ESP_UART_TX_GPIO_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = ESP_UART_RX_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = ESP_UART_GPIO_AF;
    HAL_GPIO_Init(ESP_UART_RX_GPIO_PORT, &GPIO_InitStruct);

    s_esp_uart.Instance = ESP_UART;
    s_esp_uart.Init.BaudRate = ESP_UART_BAUD;
    s_esp_uart.Init.WordLength = UART_WORDLENGTH_8B;
    s_esp_uart.Init.StopBits = UART_STOPBITS_1;
    s_esp_uart.Init.Parity = UART_PARITY_NONE;
    s_esp_uart.Init.Mode = UART_MODE_TX_RX;
    s_esp_uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    s_esp_uart.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&s_esp_uart);

    HAL_NVIC_SetPriority(ESP_UART_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(ESP_UART_IRQn);

    HAL_UART_Receive_IT(&s_esp_uart, &rx_byte, 1);
    rx_last_tick = HAL_GetTick();
}

UART_HandleTypeDef *ESP_GetUartHandle(void)
{
    return &s_esp_uart;
}

void ESP_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if ((huart != NULL) && (huart->Instance == ESP_UART))
    {
        ESP_RingBuf_Push(rx_byte);
        rx_last_tick = HAL_GetTick();

        if ((at_waiting != false) && (at_resp_len < (ESP_RX_BUF_SIZE - 1U)))
        {
            at_resp_buf[at_resp_len++] = rx_byte;
            at_resp_buf[at_resp_len] = '\0';
        }

        HAL_UART_Receive_IT(&s_esp_uart, &rx_byte, 1);
    }
}

void ESP_ErrorCallback(UART_HandleTypeDef *huart)
{
    if ((huart == NULL) || (huart->Instance != ESP_UART))
    {
        return;
    }

    __HAL_UART_CLEAR_PEFLAG(&s_esp_uart);
    __HAL_UART_CLEAR_FEFLAG(&s_esp_uart);
    __HAL_UART_CLEAR_NEFLAG(&s_esp_uart);
    __HAL_UART_CLEAR_OREFLAG(&s_esp_uart);
    s_esp_uart.ErrorCode = HAL_UART_ERROR_NONE;
    s_esp_uart.RxState = HAL_UART_STATE_READY;
    (void)HAL_UART_Receive_IT(&s_esp_uart, &rx_byte, 1);
}

static void ESP_RingBuf_Push(uint8_t byte)
{
    uint16_t next = (uint16_t)((rx_head + 1U) % ESP_RX_BUF_SIZE);
    if (next != rx_tail)
    {
        rx_buf[rx_head] = byte;
        rx_head = next;
    }
    else
    {
        rx_overflow = 1U;
    }
}

static bool ESP_RingBuf_Pop(uint8_t *byte)
{
    if (rx_tail == rx_head)
    {
        return false;
    }

    *byte = rx_buf[rx_tail];
    rx_tail = (uint16_t)((rx_tail + 1U) % ESP_RX_BUF_SIZE);
    return true;
}

static void ESP_RingBuf_Reset(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    rx_head = 0U;
    rx_tail = 0U;
    if ((primask & 0x1U) == 0U)
    {
        __enable_irq();
    }
}

static void ESP_ClearAtResponse(void)
{
    at_resp_len = 0U;
    memset((void *)at_resp_buf, 0, sizeof(at_resp_buf));
}

static void ESP_ResetSendState(void)
{
    send_state = ESP_SEND_IDLE;
    send_len = 0U;
    send_start_tick = 0U;
    at_waiting = false;
}

static void ESP_ResetParseState(void)
{
    parse_state = PARSE_TEXT;
    parse_header_len = 0U;
    parse_link_id = 0xFFU;
    parse_data_len = 0U;
    parse_data_cnt = 0U;
    parse_state_tick = HAL_GetTick();
    memset(parse_header_buf, 0, sizeof(parse_header_buf));
}

static void ESP_HandleRxOverflow(void)
{
    if (rx_overflow == 0U)
    {
        return;
    }

    rx_overflow = 0U;
    ESP_RingBuf_Reset();
    ESP_ResetParseState();
    notice_len = 0U;
}

static void ESP_CheckParseTimeout(void)
{
    uint32_t now;

    if (parse_state != PARSE_DATA)
    {
        return;
    }

    now = HAL_GetTick();
    if ((now - parse_state_tick) > ESP_PARSE_DATA_TIMEOUT_MS)
    {
        ESP_ResetParseState();
        notice_len = 0U;
    }
}

static void ESP_SetClientConnected(uint8_t link_id)
{
    active_link_id = link_id;
    tcp_connected = 1U;
}

static void ESP_ClearClientConnected(void)
{
    tcp_connected = 0U;
    active_link_id = 0xFFU;
    ESP_ResetSendState();
    ESP_ResetParseState();
}

static uint8_t ESP_ParseLinkId(const char *line, uint8_t *link_id)
{
    char *end = NULL;
    unsigned long value;

    if ((line == NULL) || (link_id == NULL))
    {
        return 0U;
    }

    value = strtoul(line, &end, 10);
    if ((end == line) || (end == NULL) || (*end != ','))
    {
        return 0U;
    }

    if (value > 0xFFUL)
    {
        value = 0xFFUL;
    }

    *link_id = (uint8_t)value;
    return 1U;
}

ESP_Status_t ESP_SendAT(const char *cmd, const char *ack, uint32_t timeout)
{
    uint32_t start = HAL_GetTick();

    ESP_ClearAtResponse();
    at_waiting = true;

    HAL_UART_Transmit(&s_esp_uart, (uint8_t *)cmd, (uint16_t)strlen(cmd), 1000U);
    HAL_UART_Transmit(&s_esp_uart, (uint8_t *)"\r\n", 2U, 100U);

    while ((HAL_GetTick() - start) < timeout)
    {
        if ((ack != NULL) && (strstr((const char *)at_resp_buf, ack) != NULL))
        {
            at_waiting = false;
            return ESP_OK;
        }

        if ((strstr((const char *)at_resp_buf, "ERROR") != NULL) ||
            (strstr((const char *)at_resp_buf, "FAIL") != NULL) ||
            (strstr((const char *)at_resp_buf, "busy") != NULL))
        {
            at_waiting = false;
            return ESP_ERR_AT_FAIL;
        }

        ESP_Delay(10U);
    }

    at_waiting = false;
    return ESP_ERR_TIMEOUT;
}

ESP_Status_t ESP_Init(void)
{
    if (esp_initialized == 0U)
    {
        ESP_UART_Init();
        ESP_Delay(1000U);
        esp_initialized = 1U;
    }

    ESP_ClearAtResponse();
    ESP_ResetParseState();
    ESP_RingBuf_Reset();
    rx_overflow = 0U;
    at_waiting = false;
    wifi_connected = 0U;
    tcp_connected = 0U;
    server_ready = 0U;
    active_link_id = 0xFFU;
    conn_state = ESP_CONN_IDLE;
    async_ack = NULL;
    async_start_tick = 0U;
    async_timeout = 0U;
    send_state = ESP_SEND_IDLE;
    send_len = 0U;
    send_start_tick = 0U;
    last_reconnect_tick = HAL_GetTick() - ESP_RECONNECT_INTERVAL_MS;

    return ESP_OK;
}

ESP_Status_t ESP_SendData(uint8_t link_id, const uint8_t *data, uint16_t len)
{
    char cmd_buf[32];
    uint8_t target_link_id = active_link_id;
    uint32_t start = 0U;

    (void)link_id;

    if ((data == NULL) || (len == 0U) || (len > ESP_TX_BUF_SIZE))
    {
        return ESP_ERR_SEND_FAIL;
    }

    if ((server_ready == 0U) || (tcp_connected == 0U) || (target_link_id == 0xFFU))
    {
        return ESP_ERR_BUSY;
    }

    if ((at_waiting != false) || (send_state != ESP_SEND_IDLE))
    {
        return ESP_ERR_BUSY;
    }

    snprintf(cmd_buf, sizeof(cmd_buf), "AT+CIPSEND=%u,%u",
             (unsigned int)target_link_id, (unsigned int)len);
    if (ESP_SendAT(cmd_buf, ">", ESP_SEND_PROMPT_TIMEOUT) != ESP_OK)
    {
        ESP_ClearClientConnected();
        return ESP_ERR_SEND_FAIL;
    }

    ESP_ClearAtResponse();
    at_waiting = true;

    HAL_UART_Transmit(&s_esp_uart, (uint8_t *)data, len, 3000U);

    start = HAL_GetTick();
    while ((HAL_GetTick() - start) < ESP_SEND_RESULT_TIMEOUT)
    {
        if (strstr((const char *)at_resp_buf, "SEND OK") != NULL)
        {
            at_waiting = false;
            return ESP_OK;
        }

        if ((strstr((const char *)at_resp_buf, "SEND FAIL") != NULL) ||
            (strstr((const char *)at_resp_buf, "ERROR") != NULL) ||
            (strstr((const char *)at_resp_buf, "CLOSED") != NULL))
        {
            at_waiting = false;
            ESP_ClearClientConnected();
            return ESP_ERR_SEND_FAIL;
        }

        ESP_Delay(5U);
    }

    at_waiting = false;
    ESP_ClearClientConnected();
    return ESP_ERR_TIMEOUT;
}

ESP_Status_t ESP_SendDataAsync(uint8_t link_id, const uint8_t *data, uint16_t len)
{
    char cmd_buf[32];
    uint8_t target_link_id = active_link_id;

    (void)link_id;

    if ((data == NULL) || (len == 0U) || (len > ESP_TX_BUF_SIZE))
    {
        return ESP_ERR_SEND_FAIL;
    }

    if ((tcp_connected == 0U) ||
        (server_ready == 0U) ||
        (target_link_id == 0xFFU) ||
        (at_waiting != false) ||
        (send_state != ESP_SEND_IDLE) ||
        (conn_state != ESP_CONN_IDLE))
    {
        return ESP_ERR_BUSY;
    }

    memcpy(send_buf, data, len);
    send_len = len;

    snprintf(cmd_buf, sizeof(cmd_buf), "AT+CIPSEND=%u,%u",
             (unsigned int)target_link_id, (unsigned int)len);
    ESP_ClearAtResponse();
    at_waiting = true;
    send_state = ESP_SEND_WAIT_PROMPT;
    send_start_tick = HAL_GetTick();

    HAL_UART_Transmit(&s_esp_uart, (uint8_t *)cmd_buf, (uint16_t)strlen(cmd_buf), 1000U);
    HAL_UART_Transmit(&s_esp_uart, (uint8_t *)"\r\n", 2U, 100U);

    return ESP_OK;
}

ESP_Status_t ESP_SendString(uint8_t link_id, const char *str)
{
    if (str == NULL)
    {
        return ESP_ERR_SEND_FAIL;
    }

    return ESP_SendData(link_id, (const uint8_t *)str, (uint16_t)strlen(str));
}

static void ESP_HandleNoticeLine(const char *line)
{
    uint8_t link_id = 0xFFU;

    if (line == NULL)
    {
        return;
    }

    if ((strstr(line, "CONNECT FAIL") != NULL) ||
        (strstr(line, "link is not valid") != NULL))
    {
        ESP_ClearClientConnected();
        return;
    }

    if ((strstr(line, ",CONNECT") != NULL) && (ESP_ParseLinkId(line, &link_id) != 0U))
    {
        ESP_SetClientConnected(link_id);
        return;
    }

    if (((strstr(line, ",CLOSED") != NULL) || (strstr(line, ",DISCONNECT") != NULL)) &&
        (ESP_ParseLinkId(line, &link_id) != 0U))
    {
        if ((active_link_id == 0xFFU) || (active_link_id == link_id))
        {
            ESP_ClearClientConnected();
        }
        return;
    }

    if (strstr(line, "CLOSED") != NULL)
    {
        ESP_ClearClientConnected();
        return;
    }

    if ((strstr(line, "WIFI CONNECTED") != NULL) ||
        (strstr(line, "WIFI GOT IP") != NULL))
    {
        wifi_connected = 1U;
        return;
    }

    if ((strstr(line, "STA_CONNECTED") != NULL) ||
        (strstr(line, "AP CONNECTED") != NULL))
    {
        wifi_connected = 1U;
        return;
    }

    if ((strstr(line, "STA_DISCONNECTED") != NULL) ||
        (strstr(line, "AP DISCONNECTED") != NULL))
    {
        ESP_ClearClientConnected();
        return;
    }

    if (strstr(line, "WIFI DISCONNECT") != NULL)
    {
        ESP_ClearClientConnected();
        return;
    }
}

static void ESP_ConnectionAbort(void)
{
    at_waiting = false;
    async_ack = NULL;
    async_start_tick = 0U;
    async_timeout = 0U;
    conn_state = ESP_CONN_IDLE;
    wifi_connected = 0U;
    server_ready = 0U;
    ESP_ClearClientConnected();
    last_reconnect_tick = HAL_GetTick();
}

static void ESP_ConnectionAdvance(void)
{
    switch (conn_state)
    {
    case ESP_CONN_AT:
        conn_state = ESP_CONN_ATE0;
        break;

    case ESP_CONN_ATE0:
        conn_state = ESP_CONN_CWMODE;
        break;

    case ESP_CONN_CWMODE:
        conn_state = ESP_CONN_CWSAP;
        break;

    case ESP_CONN_CWSAP:
        wifi_connected = 1U;
        conn_state = ESP_CONN_CIPMUX;
        break;

    case ESP_CONN_CIPMUX:
        conn_state = ESP_CONN_CIPDINFO;
        break;

    case ESP_CONN_CIPDINFO:
        conn_state = ESP_CONN_CIPSERVER;
        break;

    case ESP_CONN_CIPSERVER:
        server_ready = 1U;
        async_ack = NULL;
        async_start_tick = 0U;
        async_timeout = 0U;
        conn_state = ESP_CONN_IDLE;
        last_reconnect_tick = HAL_GetTick();
        break;

    default:
        conn_state = ESP_CONN_IDLE;
        break;
    }
}

static uint8_t ESP_StartAsyncAT(const char *cmd, const char *ack, uint32_t timeout)
{
    if ((cmd == NULL) || (ack == NULL) || (at_waiting != false))
    {
        return 0U;
    }

    ESP_ClearAtResponse();
    at_waiting = true;
    async_ack = ack;
    async_start_tick = HAL_GetTick();
    async_timeout = timeout;

    HAL_UART_Transmit(&s_esp_uart, (uint8_t *)cmd, (uint16_t)strlen(cmd), 1000U);
    HAL_UART_Transmit(&s_esp_uart, (uint8_t *)"\r\n", 2U, 100U);

    return 1U;
}

static ESP_AsyncResult_t ESP_PollAsyncAT(void)
{
    if (at_waiting == false)
    {
        return ESP_ASYNC_FAIL;
    }

    if ((async_ack != NULL) && (strstr((const char *)at_resp_buf, async_ack) != NULL))
    {
        at_waiting = false;
        async_ack = NULL;
        async_start_tick = 0U;
        async_timeout = 0U;
        return ESP_ASYNC_OK;
    }

    if ((strstr((const char *)at_resp_buf, "ERROR") != NULL) ||
        (strstr((const char *)at_resp_buf, "FAIL") != NULL) ||
        (strstr((const char *)at_resp_buf, "busy") != NULL))
    {
        at_waiting = false;
        async_ack = NULL;
        async_start_tick = 0U;
        async_timeout = 0U;
        return ESP_ASYNC_FAIL;
    }

    if ((HAL_GetTick() - async_start_tick) >= async_timeout)
    {
        at_waiting = false;
        async_ack = NULL;
        async_start_tick = 0U;
        async_timeout = 0U;
        return ESP_ASYNC_FAIL;
    }

    return ESP_ASYNC_PENDING;
}

static void ESP_ProcessSend(void)
{
    if (send_state == ESP_SEND_IDLE)
    {
        return;
    }

    if (tcp_connected == 0U)
    {
        ESP_ResetSendState();
        return;
    }

    if (send_state == ESP_SEND_WAIT_PROMPT)
    {
        if (strstr((const char *)at_resp_buf, ">") != NULL)
        {
            ESP_ClearAtResponse();
            at_waiting = true;
            send_state = ESP_SEND_WAIT_RESULT;
            send_start_tick = HAL_GetTick();
            HAL_UART_Transmit(&s_esp_uart, send_buf, send_len, 1000U);
            return;
        }

        if ((strstr((const char *)at_resp_buf, "ERROR") != NULL) ||
            (strstr((const char *)at_resp_buf, "FAIL") != NULL) ||
            (strstr((const char *)at_resp_buf, "busy") != NULL) ||
            ((HAL_GetTick() - send_start_tick) >= ESP_SEND_PROMPT_TIMEOUT))
        {
            ESP_ClearClientConnected();
            ESP_ResetSendState();
        }

        return;
    }

    if (send_state == ESP_SEND_WAIT_RESULT)
    {
        if (strstr((const char *)at_resp_buf, "SEND OK") != NULL)
        {
            ESP_ResetSendState();
            return;
        }

        if ((strstr((const char *)at_resp_buf, "SEND FAIL") != NULL) ||
            (strstr((const char *)at_resp_buf, "ERROR") != NULL) ||
            (strstr((const char *)at_resp_buf, "CLOSED") != NULL) ||
            ((HAL_GetTick() - send_start_tick) >= ESP_SEND_RESULT_TIMEOUT))
        {
            ESP_ClearClientConnected();
            ESP_ResetSendState();
        }
    }
}

static void ESP_ProcessConnection(void)
{
    ESP_AsyncResult_t async_result = ESP_ASYNC_PENDING;

    if (esp_initialized == 0U)
    {
        return;
    }

    if (server_ready != 0U)
    {
        conn_state = ESP_CONN_IDLE;
        return;
    }

    if (send_state != ESP_SEND_IDLE)
    {
        return;
    }

    if (at_waiting != false)
    {
        async_result = ESP_PollAsyncAT();
        if (async_result == ESP_ASYNC_PENDING)
        {
            return;
        }

        if (async_result == ESP_ASYNC_OK)
        {
            ESP_ConnectionAdvance();
        }
        else
        {
            ESP_ConnectionAbort();
            return;
        }
    }

    if (conn_state == ESP_CONN_IDLE)
    {
        if ((HAL_GetTick() - last_reconnect_tick) < ESP_RECONNECT_INTERVAL_MS)
        {
            return;
        }

        conn_state = ESP_CONN_AT;
    }

    switch (conn_state)
    {
    case ESP_CONN_AT:
        (void)ESP_StartAsyncAT("AT", "OK", ESP_AT_TIMEOUT);
        break;

    case ESP_CONN_ATE0:
        (void)ESP_StartAsyncAT("ATE0", "OK", ESP_AT_TIMEOUT);
        break;

    case ESP_CONN_CWMODE:
        (void)ESP_StartAsyncAT("AT+CWMODE=2", "OK", ESP_AT_TIMEOUT);
        break;

    case ESP_CONN_CWSAP:
        snprintf(async_cmd_buf, sizeof(async_cmd_buf), "AT+CWSAP=\"%s\",\"%s\",%u,%u",
                 ESP_WIFI_SSID, ESP_WIFI_PASS,
                 (unsigned int)ESP_AP_CHANNEL,
                 (unsigned int)ESP_AP_AUTHMODE);
        (void)ESP_StartAsyncAT(async_cmd_buf, "OK", ESP_AP_SETUP_TIMEOUT);
        break;

    case ESP_CONN_CIPMUX:
        (void)ESP_StartAsyncAT("AT+CIPMUX=1", "OK", ESP_AT_TIMEOUT);
        break;

    case ESP_CONN_CIPDINFO:
        (void)ESP_StartAsyncAT("AT+CIPDINFO=0", "OK", ESP_AT_TIMEOUT);
        break;

    case ESP_CONN_CIPSERVER:
        snprintf(async_cmd_buf, sizeof(async_cmd_buf), "AT+CIPSERVER=1,%u", (unsigned int)ESP_TCP_PORT);
        (void)ESP_StartAsyncAT(async_cmd_buf, "OK", ESP_TCP_TIMEOUT);
        break;

    default:
        break;
    }
}

static void ESP_HandleNoticeChar(uint8_t ch)
{
    if (ch == '\r')
    {
        return;
    }

    if (ch == '\n')
    {
        if (notice_len > 0U)
        {
            notice_buf[notice_len] = '\0';
            ESP_HandleNoticeLine(notice_buf);
            notice_len = 0U;
        }
        return;
    }

    if (notice_len < (ESP_NOTICE_BUF_SIZE - 1U))
    {
        notice_buf[notice_len++] = (char)ch;
    }
    else
    {
        notice_len = 0U;
    }
}

static void ESP_ParseIncoming(void)
{
    uint8_t ch = 0U;

    while (ESP_RingBuf_Pop(&ch))
    {
        switch (parse_state)
        {
        case PARSE_TEXT:
            if (ch == '+')
            {
                parse_state = PARSE_PLUS;
            }
            else
            {
                ESP_HandleNoticeChar(ch);
            }
            break;

        case PARSE_PLUS:
            if (ch == 'I')
            {
                parse_state = PARSE_I;
            }
            else
            {
                ESP_HandleNoticeChar('+');
                ESP_HandleNoticeChar(ch);
                parse_state = PARSE_TEXT;
            }
            break;

        case PARSE_I:
            if (ch == 'P')
            {
                parse_state = PARSE_P;
            }
            else
            {
                ESP_HandleNoticeChar('+');
                ESP_HandleNoticeChar('I');
                ESP_HandleNoticeChar(ch);
                parse_state = PARSE_TEXT;
            }
            break;

        case PARSE_P:
            if (ch == 'D')
            {
                parse_state = PARSE_D;
            }
            else
            {
                ESP_HandleNoticeChar('+');
                ESP_HandleNoticeChar('I');
                ESP_HandleNoticeChar('P');
                ESP_HandleNoticeChar(ch);
                parse_state = PARSE_TEXT;
            }
            break;

        case PARSE_D:
            if (ch == ',')
            {
                parse_header_len = 0U;
                parse_data_len = 0U;
                parse_link_id = 0xFFU;
                parse_state = PARSE_HEADER;
                parse_state_tick = HAL_GetTick();
            }
            else
            {
                ESP_HandleNoticeChar('+');
                ESP_HandleNoticeChar('I');
                ESP_HandleNoticeChar('P');
                ESP_HandleNoticeChar('D');
                ESP_HandleNoticeChar(ch);
                parse_state = PARSE_TEXT;
            }
            break;

        case PARSE_HEADER:
            if (ch == ':')
            {
                unsigned int len = 0U;
                unsigned int link = 0U;

                parse_header_buf[parse_header_len] = '\0';
                if (parse_header_len == 0U)
                {
                    parse_state = PARSE_TEXT;
                    break;
                }

                if (strchr(parse_header_buf, ',') != NULL)
                {
                    if (sscanf(parse_header_buf, "%u,%u", &link, &len) != 2)
                    {
                        parse_state = PARSE_TEXT;
                        break;
                    }
                    if (link > 0xFFU)
                    {
                        link = 0xFFU;
                    }
                    parse_link_id = (uint8_t)link;
                }
                else
                {
                    if (sscanf(parse_header_buf, "%u", &len) != 1)
                    {
                        parse_state = PARSE_TEXT;
                        break;
                    }
                    parse_link_id = 0U;
                }

                if (len == 0U)
                {
                    parse_state = PARSE_TEXT;
                    break;
                }

                parse_data_len = (len > 0xFFFFU) ? 0xFFFFU : (uint16_t)len;
                parse_data_cnt = 0U;
                parse_state = PARSE_DATA;
                parse_state_tick = HAL_GetTick();
            }
            else if (parse_header_len < (sizeof(parse_header_buf) - 1U))
            {
                parse_header_buf[parse_header_len++] = (char)ch;
            }
            else
            {
                parse_state = PARSE_TEXT;
            }
            break;

        case PARSE_DATA:
            if (parse_data_cnt < ESP_DATA_BUF_SIZE)
            {
                parse_packet.data[parse_data_cnt] = ch;
            }
            parse_data_cnt++;
            parse_state_tick = HAL_GetTick();

            if (parse_data_cnt >= parse_data_len)
            {
                parse_packet.len = (parse_data_len > ESP_DATA_BUF_SIZE) ? ESP_DATA_BUF_SIZE : parse_data_len;
                parse_packet.link_id = parse_link_id;
                ESP_SetClientConnected(parse_packet.link_id);
                if (user_callback != NULL)
                {
                    user_callback(&parse_packet);
                }
                parse_state = PARSE_TEXT;
            }
            break;

        default:
            parse_state = PARSE_TEXT;
            break;
        }
    }
}

void ESP_Process(void)
{
    ESP_HandleRxOverflow();
    ESP_CheckParseTimeout();
    ESP_ParseIncoming();
    ESP_ProcessSend();
    ESP_ProcessConnection();
}

void ESP_RegisterCallback(ESP_RxCallback_t cb)
{
    user_callback = cb;
}

uint8_t ESP_IsWifiConnected(void)
{
    return (uint8_t)((server_ready != 0U) || (wifi_connected != 0U));
}

uint8_t ESP_IsConnected(void)
{
    return tcp_connected;
}

static void ESP_Delay(uint32_t ms)
{
    HAL_Delay(ms);
}
