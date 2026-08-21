#include ".\\Hardware\\ESP_01S\\ESP_01S.h"

#include <stdio.h>
#include <string.h>

#define ESP_NOTICE_BUF_SIZE 128U
#define ESP_RESET_TIMEOUT 5000U

typedef enum
{
    PARSE_TEXT = 0,
    PARSE_PLUS,
    PARSE_I,
    PARSE_P,
    PARSE_D,
    PARSE_COMMA,
    PARSE_LENGTH,
    PARSE_DATA
} ESP_ParseState_t;

typedef enum
{
    ESP_CONN_IDLE = 0,
    ESP_CONN_AT,
    ESP_CONN_ATE0,
    ESP_CONN_CWMODE,
    ESP_CONN_CWJAP,
    ESP_CONN_CIPMUX,
    ESP_CONN_CIPDINFO,
    ESP_CONN_CIPCLOSE,
    ESP_CONN_CIPSTART
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
static uint8_t rx_byte = 0U;

static volatile uint8_t at_resp_buf[ESP_RX_BUF_SIZE];
static volatile uint16_t at_resp_len = 0U;
static volatile bool at_waiting = false;

static ESP_RxCallback_t user_callback = NULL;
static uint8_t esp_initialized = 0U;
static uint8_t wifi_connected = 0U;
static uint8_t tcp_connected = 0U;
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
static uint16_t parse_data_len = 0U;
static uint16_t parse_data_cnt = 0U;
static ESP_DataPacket_t parse_packet = {{0}, 0U, 0U};

static char notice_buf[ESP_NOTICE_BUF_SIZE];
static uint16_t notice_len = 0U;

static void ESP_UART_Init(void);
static void ESP_RingBuf_Push(uint8_t byte);
static bool ESP_RingBuf_Pop(uint8_t *byte);
static void ESP_ClearAtResponse(void);
static void ESP_HandleNoticeChar(uint8_t ch);
static void ESP_HandleNoticeLine(const char *line);
static void ESP_ParseIncoming(void);
static void ESP_Delay(uint32_t ms);
static void ESP_ConnectionAbort(void);
static void ESP_ConnectionAdvance(void);
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
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ESP_UART_TX_GPIO_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = ESP_UART_RX_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
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
}

void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&s_esp_uart);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == ESP_UART)
    {
        ESP_RingBuf_Push(rx_byte);

        if ((at_waiting != false) && (at_resp_len < (ESP_RX_BUF_SIZE - 1U)))
        {
            at_resp_buf[at_resp_len++] = rx_byte;
            at_resp_buf[at_resp_len] = '\0';
        }

        HAL_UART_Receive_IT(&s_esp_uart, &rx_byte, 1);
    }
}

static void ESP_RingBuf_Push(uint8_t byte)
{
    uint16_t next = (uint16_t)((rx_head + 1U) % ESP_RX_BUF_SIZE);
    if (next != rx_tail)
    {
        rx_buf[rx_head] = byte;
        rx_head = next;
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
    at_waiting = false;
    wifi_connected = 0U;
    tcp_connected = 0U;
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
    uint32_t start = 0U;

    (void)link_id;

    if ((data == NULL) || (len == 0U) || (len > ESP_TX_BUF_SIZE))
    {
        return ESP_ERR_SEND_FAIL;
    }

    if (tcp_connected == 0U)
    {
        return ESP_ERR_BUSY;
    }

    if ((at_waiting != false) || (send_state != ESP_SEND_IDLE))
    {
        return ESP_ERR_BUSY;
    }

    snprintf(cmd_buf, sizeof(cmd_buf), "AT+CIPSEND=%u", (unsigned int)len);
    if (ESP_SendAT(cmd_buf, ">", ESP_SEND_PROMPT_TIMEOUT) != ESP_OK)
    {
        tcp_connected = 0U;
        conn_state = ESP_CONN_IDLE;
        last_reconnect_tick = HAL_GetTick();
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
            tcp_connected = 0U;
            conn_state = ESP_CONN_IDLE;
            last_reconnect_tick = HAL_GetTick();
            return ESP_ERR_SEND_FAIL;
        }

        ESP_Delay(5U);
    }

    at_waiting = false;
    tcp_connected = 0U;
    conn_state = ESP_CONN_IDLE;
    last_reconnect_tick = HAL_GetTick();
    return ESP_ERR_TIMEOUT;
}

ESP_Status_t ESP_SendDataAsync(uint8_t link_id, const uint8_t *data, uint16_t len)
{
    char cmd_buf[32];

    (void)link_id;

    if ((data == NULL) || (len == 0U) || (len > ESP_TX_BUF_SIZE))
    {
        return ESP_ERR_SEND_FAIL;
    }

    if ((tcp_connected == 0U) ||
        (at_waiting != false) ||
        (send_state != ESP_SEND_IDLE) ||
        (conn_state != ESP_CONN_IDLE))
    {
        return ESP_ERR_BUSY;
    }

    memcpy(send_buf, data, len);
    send_len = len;

    snprintf(cmd_buf, sizeof(cmd_buf), "AT+CIPSEND=%u", (unsigned int)len);
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
    if (line == NULL)
    {
        return;
    }

    if ((strstr(line, "CLOSED") != NULL) ||
        (strstr(line, "CONNECT FAIL") != NULL) ||
        (strstr(line, "link is not valid") != NULL))
    {
        tcp_connected = 0U;
        conn_state = ESP_CONN_IDLE;
        ESP_ResetSendState();
        last_reconnect_tick = HAL_GetTick();
    }

    if ((strstr(line, "WIFI CONNECTED") != NULL) ||
        (strstr(line, "WIFI GOT IP") != NULL))
    {
        wifi_connected = 1U;
    }

    if (strstr(line, "WIFI DISCONNECT") != NULL)
    {
        wifi_connected = 0U;
        tcp_connected = 0U;
        conn_state = ESP_CONN_IDLE;
        ESP_ResetSendState();
        last_reconnect_tick = HAL_GetTick();
    }
}

static void ESP_ConnectionAbort(void)
{
    at_waiting = false;
    async_ack = NULL;
    async_start_tick = 0U;
    async_timeout = 0U;
    conn_state = ESP_CONN_IDLE;
    tcp_connected = 0U;
    ESP_ResetSendState();
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
        conn_state = (wifi_connected != 0U) ? ESP_CONN_CIPMUX : ESP_CONN_CWJAP;
        break;

    case ESP_CONN_CWJAP:
        wifi_connected = 1U;
        conn_state = ESP_CONN_CIPMUX;
        break;

    case ESP_CONN_CIPMUX:
        conn_state = ESP_CONN_CIPDINFO;
        break;

    case ESP_CONN_CIPDINFO:
        conn_state = ESP_CONN_CIPCLOSE;
        break;

    case ESP_CONN_CIPCLOSE:
        conn_state = ESP_CONN_CIPSTART;
        break;

    case ESP_CONN_CIPSTART:
        tcp_connected = 1U;
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
            tcp_connected = 0U;
            conn_state = ESP_CONN_IDLE;
            ESP_ResetSendState();
            last_reconnect_tick = HAL_GetTick();
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
            tcp_connected = 0U;
            conn_state = ESP_CONN_IDLE;
            ESP_ResetSendState();
            last_reconnect_tick = HAL_GetTick();
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

    if (tcp_connected != 0U)
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
        else if ((conn_state == ESP_CONN_CIPDINFO) || (conn_state == ESP_CONN_CIPCLOSE))
        {
            ESP_ConnectionAdvance();
        }
        else if (conn_state == ESP_CONN_CWJAP)
        {
            wifi_connected = 0U;
            ESP_ConnectionAbort();
            return;
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
        (void)ESP_StartAsyncAT("AT+CWMODE=1", "OK", ESP_AT_TIMEOUT);
        break;

    case ESP_CONN_CWJAP:
        snprintf(async_cmd_buf, sizeof(async_cmd_buf), "AT+CWJAP=\"%s\",\"%s\"", ESP_WIFI_SSID, ESP_WIFI_PASS);
        (void)ESP_StartAsyncAT(async_cmd_buf, "OK", ESP_WIFI_JOIN_TIMEOUT);
        break;

    case ESP_CONN_CIPMUX:
        (void)ESP_StartAsyncAT("AT+CIPMUX=0", "OK", ESP_AT_TIMEOUT);
        break;

    case ESP_CONN_CIPDINFO:
        (void)ESP_StartAsyncAT("AT+CIPDINFO=0", "OK", ESP_AT_TIMEOUT);
        break;

    case ESP_CONN_CIPCLOSE:
        (void)ESP_StartAsyncAT("AT+CIPCLOSE", "OK", 1000U);
        break;

    case ESP_CONN_CIPSTART:
        snprintf(async_cmd_buf, sizeof(async_cmd_buf), "AT+CIPSTART=\"TCP\",\"%s\",%u", ESP_TCP_HOST, (unsigned int)ESP_TCP_PORT);
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
                parse_state = PARSE_LENGTH;
                parse_data_len = 0U;
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

        case PARSE_LENGTH:
            if ((ch >= '0') && (ch <= '9'))
            {
                parse_data_len = (uint16_t)(parse_data_len * 10U + (uint16_t)(ch - '0'));
            }
            else if (ch == ':')
            {
                parse_data_cnt = 0U;
                parse_state = PARSE_DATA;
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

            if (parse_data_cnt >= parse_data_len)
            {
                parse_packet.len = (parse_data_len > ESP_DATA_BUF_SIZE) ? ESP_DATA_BUF_SIZE : parse_data_len;
                parse_packet.link_id = 0U;
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
    return wifi_connected;
}

uint8_t ESP_IsConnected(void)
{
    return tcp_connected;
}

static void ESP_Delay(uint32_t ms)
{
    HAL_Delay(ms);
}
