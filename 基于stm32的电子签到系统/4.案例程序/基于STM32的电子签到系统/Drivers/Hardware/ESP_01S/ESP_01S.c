#include ".\Hardware\ESP_01S\ESP_01S.h"

#include <stdio.h>
#include <string.h>

#define ESP_AT_RESP_BUF_SIZE 512u
#define ESP_FALLBACK_BAUD 9600u

typedef enum
{
    ESP_PARSE_IDLE = 0,
    ESP_PARSE_HEADER,
    ESP_PARSE_LINK_ID,
    ESP_PARSE_LENGTH,
    ESP_PARSE_PAYLOAD,
    ESP_PARSE_EVENT_LINE
} ESP_ParseState_t;

static UART_HandleTypeDef s_esp_uart;

static volatile uint8_t s_rx_buffer[ESP_RX_BUF_SIZE];
static volatile uint16_t s_rx_head = 0u;
static volatile uint16_t s_rx_tail = 0u;
static uint8_t s_rx_byte = 0u;

static volatile uint8_t s_at_response[ESP_AT_RESP_BUF_SIZE];
static volatile uint16_t s_at_response_length = 0u;
static volatile bool s_at_waiting = false;

static ESP_RxCallback_t s_user_callback = NULL;
static uint8_t s_uart_initialized = 0u;
static uint8_t s_last_error_step = 0u;
static uint32_t s_active_baud = ESP_UART_BAUD;
static uint8_t s_active_link_mask = 0u;

static ESP_ParseState_t s_parse_state = ESP_PARSE_IDLE;
static char s_header_buffer[4];
static uint8_t s_header_index = 0u;
static char s_event_line[16];
static uint8_t s_event_index = 0u;
static uint8_t s_link_id = 0u;
static uint16_t s_payload_total_length = 0u;
static uint16_t s_payload_received = 0u;
static ESP_DataPacket_t s_packet;

static void ESP_UART_Init(uint32_t baud_rate);
static void ESP_Delay(uint32_t ms);
static void ESP_RingBufferPush(uint8_t byte);
static bool ESP_RingBufferPop(uint8_t *byte);
static void ESP_ClearAtResponse(void);
static void ESP_UART_Recover(void);
static void ESP_ParseIncomingData(void);
static void ESP_HandleEventLine(void);
static ESP_Status_t ESP_StartServer(void);

static void ESP_UART_Init(uint32_t baud_rate)
{
    GPIO_InitTypeDef gpio_init = {0};

    if (s_uart_initialized != 0u)
    {
        (void)HAL_UART_AbortReceive_IT(&s_esp_uart);
        (void)HAL_UART_DeInit(&s_esp_uart);
    }

    ESP_UART_CLK_ENABLE();
    ESP_GPIO_CLK_ENABLE();

    gpio_init.Pin = ESP_TX_PIN;
    gpio_init.Mode = GPIO_MODE_AF_PP;
    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ESP_PORT, &gpio_init);

    gpio_init.Pin = ESP_RX_PIN;
    gpio_init.Mode = GPIO_MODE_INPUT;
    gpio_init.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(ESP_PORT, &gpio_init);

    s_esp_uart.Instance = ESP_UART;
    s_esp_uart.Init.BaudRate = baud_rate;
    s_esp_uart.Init.WordLength = UART_WORDLENGTH_8B;
    s_esp_uart.Init.StopBits = UART_STOPBITS_1;
    s_esp_uart.Init.Parity = UART_PARITY_NONE;
    s_esp_uart.Init.Mode = UART_MODE_TX_RX;
    s_esp_uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    s_esp_uart.Init.OverSampling = UART_OVERSAMPLING_16;

    HAL_UART_Init(&s_esp_uart);
    s_uart_initialized = 1u;
    s_active_baud = baud_rate;

    HAL_NVIC_SetPriority(ESP_UART_IRQn, 1u, 0u);
    HAL_NVIC_EnableIRQ(ESP_UART_IRQn);

    s_rx_head = 0u;
    s_rx_tail = 0u;
    ESP_ClearAtResponse();
    HAL_UART_Receive_IT(&s_esp_uart, &s_rx_byte, 1u);
}

static void ESP_Delay(uint32_t ms)
{
    HAL_Delay(ms);
}

static void ESP_RingBufferPush(uint8_t byte)
{
    uint16_t next_head = (uint16_t)((s_rx_head + 1u) % ESP_RX_BUF_SIZE);

    if (next_head == s_rx_tail)
    {
        return;
    }

    s_rx_buffer[s_rx_head] = byte;
    s_rx_head = next_head;
}

static bool ESP_RingBufferPop(uint8_t *byte)
{
    if (s_rx_tail == s_rx_head)
    {
        return false;
    }

    *byte = s_rx_buffer[s_rx_tail];
    s_rx_tail = (uint16_t)((s_rx_tail + 1u) % ESP_RX_BUF_SIZE);
    return true;
}

static void ESP_ClearAtResponse(void)
{
    s_at_response_length = 0u;
    memset((void *)s_at_response, 0, sizeof(s_at_response));
}

static void ESP_UART_Recover(void)
{
    __HAL_UART_CLEAR_PEFLAG(&s_esp_uart);
    __HAL_UART_CLEAR_FEFLAG(&s_esp_uart);
    __HAL_UART_CLEAR_NEFLAG(&s_esp_uart);
    __HAL_UART_CLEAR_OREFLAG(&s_esp_uart);

    s_rx_head = 0u;
    s_rx_tail = 0u;
    s_active_link_mask = 0u;
    s_at_waiting = false;
    ESP_ClearAtResponse();

    s_esp_uart.ErrorCode = HAL_UART_ERROR_NONE;
    s_esp_uart.RxState = HAL_UART_STATE_READY;
    HAL_UART_Receive_IT(&s_esp_uart, &s_rx_byte, 1u);
}

static void ESP_HandleEventLine(void)
{
    uint8_t link_id;

    if ((s_event_index < 8u) || (s_event_line[0] < '0') || (s_event_line[0] > '4'))
    {
        return;
    }

    link_id = (uint8_t)(s_event_line[0] - '0');
    if (strstr(s_event_line, ",CONNECT") != NULL)
    {
        s_active_link_mask |= (uint8_t)(1u << link_id);
    }
    else if (strstr(s_event_line, ",CLOSED") != NULL)
    {
        s_active_link_mask &= (uint8_t)(~(uint8_t)(1u << link_id));
    }
}

ESP_Status_t ESP_SendAT(const char *cmd, const char *ack, uint32_t timeout)
{
    uint32_t tick_start;

    if ((cmd == NULL) || (ack == NULL))
    {
        return ESP_ERR_AT_FAIL;
    }

    ESP_ClearAtResponse();
    s_at_waiting = true;

    HAL_UART_Transmit(&s_esp_uart, (uint8_t *)cmd, (uint16_t)strlen(cmd), 1000u);
    HAL_UART_Transmit(&s_esp_uart, (uint8_t *)"\r\n", 2u, 100u);

    tick_start = HAL_GetTick();
    while ((HAL_GetTick() - tick_start) < timeout)
    {
        if (strstr((const char *)s_at_response, ack) != NULL)
        {
            s_at_waiting = false;
            return ESP_OK;
        }

        if (strstr((const char *)s_at_response, "ERROR") != NULL)
        {
            s_at_waiting = false;
            return ESP_ERR_AT_FAIL;
        }

        ESP_Delay(10u);
    }

    s_at_waiting = false;
    return ESP_ERR_TIMEOUT;
}

static ESP_Status_t ESP_StartServer(void)
{
    char command[64];
    uint8_t retry;

    s_active_link_mask = 0u;
    for (retry = 0u; retry < 2u; retry++)
    {
        (void)ESP_SendAT("AT+CIPSERVER=0", "OK", 1500u);
        ESP_Delay(200u);

        if (ESP_SendAT("AT+CIPMUX=1", "OK", ESP_AT_TIMEOUT) != ESP_OK)
        {
            continue;
        }

        snprintf(command, sizeof(command), "AT+CIPSERVER=1,%s", ESP_SERVER_PORT);
        if (ESP_SendAT(command, "OK", ESP_AT_TIMEOUT) == ESP_OK)
        {
            (void)ESP_SendAT("AT+CIPSTO=30", "OK", ESP_AT_TIMEOUT);
            return ESP_OK;
        }

        ESP_Delay(500u);
    }

    return ESP_ERR_AT_FAIL;
}

ESP_Status_t ESP_Init(void)
{
    char command[128];
    static const uint32_t baud_list[] = {ESP_UART_BAUD, ESP_FALLBACK_BAUD};
    uint8_t baud_index;
    uint8_t at_ready;

    s_active_link_mask = 0u;
    s_last_error_step = 1u;
    at_ready = 0u;
    for (baud_index = 0u; baud_index < (uint8_t)(sizeof(baud_list) / sizeof(baud_list[0])); baud_index++)
    {
        ESP_UART_Init(baud_list[baud_index]);
        ESP_Delay(500u);
        if (ESP_SendAT("AT", "OK", ESP_AT_TIMEOUT) == ESP_OK)
        {
            at_ready = 1u;
            break;
        }
        (void)ESP_SendAT("AT+RST", "ready", 5000u);
        ESP_Delay(1500u);
        if (ESP_SendAT("AT", "OK", ESP_AT_TIMEOUT) == ESP_OK)
        {
            at_ready = 1u;
            break;
        }
    }

    if (at_ready == 0u)
    {
        return ESP_ERR_AT_FAIL;
    }

    (void)ESP_SendAT("ATE0", "OK", ESP_AT_TIMEOUT);
    (void)ESP_SendAT("AT+CWAUTOCONN=0", "OK", 1000u);

    s_last_error_step = 2u;
    if ((ESP_SendAT("AT+CWMODE_CUR=2", "OK", ESP_AT_TIMEOUT) != ESP_OK) &&
        (ESP_SendAT("AT+CWMODE=2", "OK", ESP_AT_TIMEOUT) != ESP_OK))
    {
        return ESP_ERR_AT_FAIL;
    }

    snprintf(command, sizeof(command), "AT+CIPAP_CUR=\"%s\",\"%s\",\"255.255.255.0\"", ESP_AP_IP, ESP_AP_IP);
    if (ESP_SendAT(command, "OK", 1000u) != ESP_OK)
    {
        snprintf(command, sizeof(command), "AT+CIPAP=\"%s\",\"%s\",\"255.255.255.0\"", ESP_AP_IP, ESP_AP_IP);
        (void)ESP_SendAT(command, "OK", 1000u);
    }
    if (ESP_SendAT("AT+CWDHCP_CUR=0,1", "OK", 1000u) != ESP_OK)
    {
        (void)ESP_SendAT("AT+CWDHCP=0,1", "OK", 1000u);
    }

    s_last_error_step = 3u;
    snprintf(command, sizeof(command), "AT+CWSAP_CUR=\"%s\",\"%s\",1,4", ESP_WIFI_SSID, ESP_WIFI_PASS);
    if (ESP_SendAT(command, "OK", ESP_AT_TIMEOUT) != ESP_OK)
    {
        snprintf(command, sizeof(command), "AT+CWSAP=\"%s\",\"%s\",1,4", ESP_WIFI_SSID, ESP_WIFI_PASS);
        if (ESP_SendAT(command, "OK", ESP_AT_TIMEOUT) != ESP_OK)
        {
            return ESP_ERR_AT_FAIL;
        }
    }

    (void)ESP_SendAT("AT+CIPDINFO=0", "OK", 1000u);
    s_last_error_step = 4u;
    if (ESP_StartServer() != ESP_OK)
    {
        return ESP_ERR_AT_FAIL;
    }

    s_last_error_step = 0u;
    return ESP_OK;
}

ESP_Status_t ESP_SendData(uint8_t link_id, const uint8_t *data, uint16_t len)
{
    char command[32];
    uint32_t tick_start;

    if ((data == NULL) || (len == 0u) || (len > ESP_TX_BUF_SIZE))
    {
        return ESP_ERR_SEND_FAIL;
    }

    snprintf(command, sizeof(command), "AT+CIPSEND=%u,%u", (unsigned int)link_id, (unsigned int)len);
    if (ESP_SendAT(command, ">", 3000u) != ESP_OK)
    {
        return ESP_ERR_SEND_FAIL;
    }

    ESP_ClearAtResponse();
    s_at_waiting = true;

    HAL_UART_Transmit(&s_esp_uart, (uint8_t *)data, len, 5000u);

    tick_start = HAL_GetTick();
    while ((HAL_GetTick() - tick_start) < 5000u)
    {
        if (strstr((const char *)s_at_response, "SEND OK") != NULL)
        {
            s_at_waiting = false;
            return ESP_OK;
        }

        if ((strstr((const char *)s_at_response, "SEND FAIL") != NULL) ||
            (strstr((const char *)s_at_response, "ERROR") != NULL))
        {
            s_at_waiting = false;
            return ESP_ERR_SEND_FAIL;
        }

        ESP_Delay(5u);
    }

    s_at_waiting = false;
    return ESP_ERR_TIMEOUT;
}

ESP_Status_t ESP_SendString(uint8_t link_id, const char *str)
{
    if (str == NULL)
    {
        return ESP_ERR_SEND_FAIL;
    }

    return ESP_SendData(link_id, (const uint8_t *)str, (uint16_t)strlen(str));
}

ESP_Status_t ESP_CloseLink(uint8_t link_id)
{
    char command[24];

    snprintf(command, sizeof(command), "AT+CIPCLOSE=%u", (unsigned int)link_id);
    if (ESP_SendAT(command, "OK", 2000u) == ESP_OK)
    {
        if (link_id < 5u)
        {
            s_active_link_mask &= (uint8_t)(~(uint8_t)(1u << link_id));
        }
        return ESP_OK;
    }

    return ESP_ERR_TIMEOUT;
}

static void ESP_ParseIncomingData(void)
{
    uint8_t byte = 0u;

    while (ESP_RingBufferPop(&byte))
    {
        if (s_at_waiting)
        {
            continue;
        }

        switch (s_parse_state)
        {
        case ESP_PARSE_IDLE:
            if (byte == '+')
            {
                s_header_index = 0u;
                s_parse_state = ESP_PARSE_HEADER;
            }
            else if ((byte >= '0') && (byte <= '4'))
            {
                s_event_index = 0u;
                memset(s_event_line, 0, sizeof(s_event_line));
                s_event_line[s_event_index++] = (char)byte;
                s_parse_state = ESP_PARSE_EVENT_LINE;
            }
            break;

        case ESP_PARSE_HEADER:
            s_header_buffer[s_header_index++] = (char)byte;
            if (s_header_index >= sizeof(s_header_buffer))
            {
                if (memcmp(s_header_buffer, "IPD,", sizeof(s_header_buffer)) == 0)
                {
                    s_link_id = 0u;
                    s_parse_state = ESP_PARSE_LINK_ID;
                }
                else
                {
                    s_parse_state = ESP_PARSE_IDLE;
                }
            }
            break;

        case ESP_PARSE_LINK_ID:
            if (byte == ',')
            {
                s_payload_total_length = 0u;
                s_parse_state = ESP_PARSE_LENGTH;
            }
            else if ((byte >= '0') && (byte <= '9'))
            {
                s_link_id = (uint8_t)((s_link_id * 10u) + (byte - '0'));
            }
            else
            {
                s_parse_state = ESP_PARSE_IDLE;
            }
            break;

        case ESP_PARSE_LENGTH:
            if (byte == ':')
            {
                s_payload_received = 0u;
                s_packet.len = 0u;
                s_packet.link_id = s_link_id;
                if (s_link_id < 5u)
                {
                    s_active_link_mask |= (uint8_t)(1u << s_link_id);
                }
                s_parse_state = ESP_PARSE_PAYLOAD;
            }
            else if ((byte >= '0') && (byte <= '9'))
            {
                s_payload_total_length = (uint16_t)((s_payload_total_length * 10u) + (byte - '0'));
            }
            else
            {
                s_parse_state = ESP_PARSE_IDLE;
            }
            break;

        case ESP_PARSE_PAYLOAD:
            if (s_packet.len < ESP_DATA_BUF_SIZE)
            {
                s_packet.data[s_packet.len++] = byte;
            }

            s_payload_received++;
            if (s_payload_received >= s_payload_total_length)
            {
                if (s_user_callback != NULL)
                {
                    s_user_callback(&s_packet);
                }
                s_parse_state = ESP_PARSE_IDLE;
            }
            break;

        case ESP_PARSE_EVENT_LINE:
            if ((byte == '\r') || (byte == '\n'))
            {
                s_event_line[s_event_index] = '\0';
                ESP_HandleEventLine();
                s_parse_state = ESP_PARSE_IDLE;
            }
            else if (s_event_index < (sizeof(s_event_line) - 1u))
            {
                s_event_line[s_event_index++] = (char)byte;
            }
            else
            {
                s_parse_state = ESP_PARSE_IDLE;
            }
            break;
        }
    }
}

void ESP_RegisterCallback(ESP_RxCallback_t cb)
{
    s_user_callback = cb;
}

void ESP_Process(void)
{
    ESP_ParseIncomingData();
}

void ESP_RxCallback(void)
{
    ESP_RingBufferPush(s_rx_byte);

    if (s_at_waiting && (s_at_response_length < (ESP_AT_RESP_BUF_SIZE - 1u)))
    {
        s_at_response[s_at_response_length++] = s_rx_byte;
        s_at_response[s_at_response_length] = '\0';
    }

    HAL_UART_Receive_IT(&s_esp_uart, &s_rx_byte, 1u);
}

void ESP_ErrorCallback(UART_HandleTypeDef *huart)
{
    if ((huart != NULL) && (huart->Instance == ESP_UART))
    {
        ESP_UART_Recover();
    }
}

uint8_t ESP_GetLastErrorStep(void)
{
    return s_last_error_step;
}

uint32_t ESP_GetActiveBaud(void)
{
    return s_active_baud;
}

uint8_t ESP_GetActiveLinkMask(void)
{
    return s_active_link_mask;
}

void ESP_UART_IRQHandler(void)
{
    HAL_UART_IRQHandler(&s_esp_uart);
}
