#include ".\Hardware\ESP_01S\ESP_01S.h"

#include <stdio.h>
#include <string.h>

static UART_HandleTypeDef esp_uart;

static volatile uint8_t rx_buf[ESP_RX_BUF_SIZE];
static volatile uint16_t rx_head = 0;
static volatile uint16_t rx_tail = 0;
static uint8_t rx_byte;

static volatile uint8_t at_resp_buf[ESP_RX_BUF_SIZE];
static volatile uint16_t at_resp_len = 0;
static volatile bool at_waiting = false;
static volatile ESP_ConnectionState_t esp_conn_state = ESP_CONN_INIT_FAIL;
static volatile uint8_t esp_client_count = 0;
static volatile uint8_t esp_active_link_id = 0;

static char status_line_buf[64];
static uint8_t status_line_len = 0;
static ESP_RxCallback_t user_callback = NULL;

typedef enum
{
    PARSE_IDLE = 0,
    PARSE_PLUS,
    PARSE_HEADER,
    PARSE_LINK_ID,
    PARSE_LENGTH,
    PARSE_DATA
} ParseState_t;

static ParseState_t parse_state = PARSE_IDLE;
static char hdr_buf[4];
static uint8_t hdr_idx;
static uint8_t parse_link_id;
static uint16_t parse_data_len;
static uint16_t parse_data_cnt;
static ESP_DataPacket_t parse_packet;

static void ESP_UART_Init(void);
static void ESP_UART_EnableClock(void);
static void ESP_GPIO_EnableClock(void);
static void ESP_RingBuf_Push(uint8_t byte);
static bool ESP_RingBuf_Pop(uint8_t *byte);
static void ESP_ParseIPD(void);
static void ESP_Delay(uint32_t ms);
static void ESP_UART_Recover(void);
static void ESP_ParseStatusLineChar(uint8_t ch);
static void ESP_HandleStatusLine(const char *line);
static void ESP_UpdateConnectionState(void);

static void ESP_UART_EnableClock(void)
{
    if (ESP_UART == USART1)
        __HAL_RCC_USART1_CLK_ENABLE();
    else if (ESP_UART == USART2)
        __HAL_RCC_USART2_CLK_ENABLE();
    else if (ESP_UART == USART3)
        __HAL_RCC_USART3_CLK_ENABLE();
}

static void ESP_GPIO_EnableClock(void)
{
    if (ESP_GPIO_PORT == GPIOA)
        __HAL_RCC_GPIOA_CLK_ENABLE();
    else if (ESP_GPIO_PORT == GPIOB)
        __HAL_RCC_GPIOB_CLK_ENABLE();
    else if (ESP_GPIO_PORT == GPIOC)
        __HAL_RCC_GPIOC_CLK_ENABLE();
    else if (ESP_GPIO_PORT == GPIOD)
        __HAL_RCC_GPIOD_CLK_ENABLE();
}

static void ESP_UART_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    ESP_UART_EnableClock();
    ESP_GPIO_EnableClock();

    GPIO_InitStruct.Pin = ESP_TX_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ESP_GPIO_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = ESP_RX_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(ESP_GPIO_PORT, &GPIO_InitStruct);

    esp_uart.Instance = ESP_UART;
    esp_uart.Init.BaudRate = ESP_UART_BAUD;
    esp_uart.Init.WordLength = UART_WORDLENGTH_8B;
    esp_uart.Init.StopBits = UART_STOPBITS_1;
    esp_uart.Init.Parity = UART_PARITY_NONE;
    esp_uart.Init.Mode = UART_MODE_TX_RX;
    esp_uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    esp_uart.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&esp_uart) != HAL_OK)
        return;

    HAL_NVIC_SetPriority(ESP_UART_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(ESP_UART_IRQn);

    rx_head = 0;
    rx_tail = 0;
    at_resp_len = 0;
    at_waiting = false;
    esp_client_count = 0;
    esp_active_link_id = 0;
    status_line_len = 0;
    status_line_buf[0] = '\0';
    HAL_UART_Receive_IT(&esp_uart, &rx_byte, 1);
}

void USART3_IRQHandler(void)
{
    HAL_UART_IRQHandler(&esp_uart);
}

void ESP_RxCallback(void)
{
    ESP_RingBuf_Push(rx_byte);

    if (at_waiting && at_resp_len < ESP_RX_BUF_SIZE - 1)
    {
        at_resp_buf[at_resp_len++] = rx_byte;
        at_resp_buf[at_resp_len] = '\0';
    }

    HAL_UART_Receive_IT(&esp_uart, &rx_byte, 1);
}

void ESP_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart == NULL || huart->Instance != ESP_UART)
        return;

    ESP_UART_Recover();
}

static void ESP_RingBuf_Push(uint8_t byte)
{
    uint16_t next = (rx_head + 1) % ESP_RX_BUF_SIZE;

    if (next != rx_tail)
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
    __HAL_UART_DISABLE_IT(&esp_uart, UART_IT_RXNE);
    __HAL_UART_DISABLE_IT(&esp_uart, UART_IT_PE);
    __HAL_UART_DISABLE_IT(&esp_uart, UART_IT_ERR);

    __HAL_UART_CLEAR_PEFLAG(&esp_uart);
    __HAL_UART_CLEAR_FEFLAG(&esp_uart);
    __HAL_UART_CLEAR_NEFLAG(&esp_uart);
    __HAL_UART_CLEAR_OREFLAG(&esp_uart);

    rx_head = 0;
    rx_tail = 0;
    at_resp_len = 0;
    at_waiting = false;
    status_line_len = 0;
    status_line_buf[0] = '\0';
    esp_uart.ErrorCode = HAL_UART_ERROR_NONE;
    esp_uart.RxState = HAL_UART_STATE_READY;
    (void)HAL_UART_Receive_IT(&esp_uart, &rx_byte, 1);
}

static void ESP_UpdateConnectionState(void)
{
    if (esp_conn_state == ESP_CONN_INIT_FAIL)
        return;

    if (esp_client_count > 0)
        esp_conn_state = ESP_CONN_CLIENT_CONNECTED;
    else
        esp_conn_state = ESP_CONN_AP_READY;
}

static void ESP_HandleStatusLine(const char *line)
{
    uint8_t link_id = 0;

    if (!line || !line[0])
        return;

    if (sscanf(line, "%hhu,CONNECT", &link_id) == 1)
    {
        esp_client_count++;
        esp_active_link_id = link_id;
        ESP_UpdateConnectionState();
        return;
    }

    if (sscanf(line, "%hhu,CLOSED", &link_id) == 1)
    {
        if (esp_client_count > 0)
            esp_client_count--;

        if (esp_client_count == 0)
            esp_active_link_id = 0;

        ESP_UpdateConnectionState();
    }
}

static void ESP_ParseStatusLineChar(uint8_t ch)
{
    if (ch == '\r' || ch == '\n')
    {
        if (status_line_len > 0)
        {
            status_line_buf[status_line_len] = '\0';
            ESP_HandleStatusLine(status_line_buf);
            status_line_len = 0;
            status_line_buf[0] = '\0';
        }
        return;
    }

    if (ch < 0x20 || ch > 0x7E)
        return;

    if (status_line_len < (sizeof(status_line_buf) - 1))
        status_line_buf[status_line_len++] = (char)ch;
}

ESP_Status_t ESP_SendAT(const char *cmd, const char *ack, uint32_t timeout)
{
    uint32_t start = 0;

    at_resp_len = 0;
    memset((void *)at_resp_buf, 0, ESP_RX_BUF_SIZE);
    at_waiting = true;

    HAL_UART_Transmit(&esp_uart, (uint8_t *)cmd, strlen(cmd), 1000);
    HAL_UART_Transmit(&esp_uart, (uint8_t *)"\r\n", 2, 100);

    start = HAL_GetTick();
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

ESP_Status_t ESP_Init(void)
{
    char cmd_buf[128];

    esp_conn_state = ESP_CONN_INIT_FAIL;
    esp_client_count = 0;
    esp_active_link_id = 0;
    status_line_len = 0;
    status_line_buf[0] = '\0';

    ESP_UART_Init();
    ESP_Delay(1000);

    if (ESP_SendAT("AT", "OK", ESP_AT_TIMEOUT) != ESP_OK)
    {
        ESP_SendAT("AT+RST", "ready", 5000);
        ESP_Delay(2000);
        if (ESP_SendAT("AT", "OK", ESP_AT_TIMEOUT) != ESP_OK)
            return ESP_ERR_AT_FAIL;
    }

    ESP_SendAT("ATE0", "OK", ESP_AT_TIMEOUT);

    if (ESP_SendAT("AT+CWMODE=2", "OK", ESP_AT_TIMEOUT) != ESP_OK)
        return ESP_ERR_AT_FAIL;

    snprintf(cmd_buf, sizeof(cmd_buf),
             "AT+CWSAP=\"%s\",\"%s\",1,4",
             ESP_WIFI_SSID, ESP_WIFI_PASS);
    if (ESP_SendAT(cmd_buf, "OK", ESP_AT_TIMEOUT) != ESP_OK)
        return ESP_ERR_AT_FAIL;

    if (ESP_SendAT("AT+CIPMUX=1", "OK", ESP_AT_TIMEOUT) != ESP_OK)
        return ESP_ERR_AT_FAIL;

    snprintf(cmd_buf, sizeof(cmd_buf), "AT+CIPSERVER=1,%s", ESP_SERVER_PORT);
    if (ESP_SendAT(cmd_buf, "OK", ESP_AT_TIMEOUT) != ESP_OK)
        return ESP_ERR_AT_FAIL;

    ESP_SendAT("AT+CIPSTO=180", "OK", ESP_AT_TIMEOUT);

    esp_conn_state = ESP_CONN_AP_READY;
    return ESP_OK;
}

ESP_Status_t ESP_SendData(uint8_t link_id, const uint8_t *data, uint16_t len)
{
    char cmd_buf[64];
    uint32_t start = 0;

    if (len == 0 || len > ESP_TX_BUF_SIZE)
        return ESP_ERR_SEND_FAIL;

    snprintf(cmd_buf, sizeof(cmd_buf), "AT+CIPSEND=%d,%d", link_id, len);
    if (ESP_SendAT(cmd_buf, ">", 2000) != ESP_OK)
        return ESP_ERR_SEND_FAIL;

    at_resp_len = 0;
    memset((void *)at_resp_buf, 0, ESP_RX_BUF_SIZE);
    at_waiting = true;

    HAL_UART_Transmit(&esp_uart, (uint8_t *)data, len, 2000);

    start = HAL_GetTick();
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

static void ESP_ParseIPD(void)
{
    uint8_t ch;

    while (ESP_RingBuf_Pop(&ch))
    {
        if (at_waiting)
            continue;

        ESP_ParseStatusLineChar(ch);

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
                parse_packet.data[parse_data_cnt++] = ch;

            if (parse_data_cnt >= parse_data_len)
            {
                parse_packet.len = parse_data_len;
                parse_packet.link_id = parse_link_id;
                if (user_callback != NULL)
                    user_callback(&parse_packet);

                parse_state = PARSE_IDLE;
            }
            break;

        default:
            parse_state = PARSE_IDLE;
            break;
        }
    }
}

void ESP_Process(void)
{
    ESP_ParseIPD();
}

void ESP_RegisterCallback(ESP_RxCallback_t cb)
{
    user_callback = cb;
}

ESP_ConnectionState_t ESP_GetConnectionState(void)
{
    return esp_conn_state;
}

uint8_t ESP_GetActiveLinkId(void)
{
    return esp_active_link_id;
}

uint8_t ESP_GetClientCount(void)
{
    return esp_client_count;
}

static void ESP_Delay(uint32_t ms)
{
    HAL_Delay(ms);
}
