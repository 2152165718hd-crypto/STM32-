#include "Hardware/ESP_01S/ESP_01S.h"

#include <stdio.h>
#include <string.h>

static UART_HandleTypeDef g_esp_uart;

static volatile uint8_t g_rx_buf[ESP_RX_BUF_SIZE];
static volatile uint16_t g_rx_head = 0u;
static volatile uint16_t g_rx_tail = 0u;
static uint8_t g_rx_byte = 0u;

static volatile uint8_t g_at_resp_buf[ESP_RX_BUF_SIZE];
static volatile uint16_t g_at_resp_len = 0u;
static volatile bool g_at_waiting = false;
static volatile ESP_ConnectionState_t g_connection_state = ESP_CONN_INIT_FAIL;
static volatile uint8_t g_client_count = 0u;
static volatile uint8_t g_active_link_id = 0u;
static volatile uint8_t g_active_link_mask = 0u;

static char g_status_line_buf[64];
static uint8_t g_status_line_len = 0u;
static ESP_RxCallback_t g_user_callback = NULL;

typedef enum
{
    PARSE_IDLE = 0,
    PARSE_PLUS,
    PARSE_HEADER,
    PARSE_LINK_ID,
    PARSE_LENGTH,
    PARSE_DATA
} ParseState_t;

static ParseState_t g_parse_state = PARSE_IDLE;
static char g_header_buf[4];
static uint8_t g_header_index = 0u;
static uint8_t g_parse_link_id = 0u;
static uint16_t g_parse_data_len = 0u;
static uint16_t g_parse_data_count = 0u;
static ESP_DataPacket_t g_parse_packet;

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
static uint8_t ESP_ParseLinkEvent(const char *line, const char *suffix, uint8_t *link_id);
static void ESP_RefreshActiveLink(void);
static void ESP_MarkLinkConnected(uint8_t link_id);
static void ESP_MarkLinkClosed(uint8_t link_id);

static void ESP_UART_EnableClock(void)
{
    if (ESP_UART == USART1)
    {
        __HAL_RCC_USART1_CLK_ENABLE();
    }
    else if (ESP_UART == USART2)
    {
        __HAL_RCC_USART2_CLK_ENABLE();
    }
    else if (ESP_UART == USART3)
    {
        __HAL_RCC_USART3_CLK_ENABLE();
    }
}

static void ESP_GPIO_EnableClock(void)
{
    if (ESP_GPIO_PORT == GPIOA)
    {
        __HAL_RCC_GPIOA_CLK_ENABLE();
    }
    else if (ESP_GPIO_PORT == GPIOB)
    {
        __HAL_RCC_GPIOB_CLK_ENABLE();
    }
    else if (ESP_GPIO_PORT == GPIOC)
    {
        __HAL_RCC_GPIOC_CLK_ENABLE();
    }
    else if (ESP_GPIO_PORT == GPIOD)
    {
        __HAL_RCC_GPIOD_CLK_ENABLE();
    }
}

static void ESP_UART_Init(void)
{
    GPIO_InitTypeDef gpio_init = {0};

    ESP_UART_EnableClock();
    ESP_GPIO_EnableClock();

    gpio_init.Pin = ESP_TX_PIN;
    gpio_init.Mode = GPIO_MODE_AF_PP;
    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio_init.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(ESP_GPIO_PORT, &gpio_init);

    gpio_init.Pin = ESP_RX_PIN;
    gpio_init.Mode = GPIO_MODE_INPUT;
    HAL_GPIO_Init(ESP_GPIO_PORT, &gpio_init);

    g_esp_uart.Instance = ESP_UART;
    g_esp_uart.Init.BaudRate = ESP_UART_BAUD;
    g_esp_uart.Init.WordLength = UART_WORDLENGTH_8B;
    g_esp_uart.Init.StopBits = UART_STOPBITS_1;
    g_esp_uart.Init.Parity = UART_PARITY_NONE;
    g_esp_uart.Init.Mode = UART_MODE_TX_RX;
    g_esp_uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    g_esp_uart.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&g_esp_uart) != HAL_OK)
    {
        return;
    }

    HAL_NVIC_SetPriority(ESP_UART_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(ESP_UART_IRQn);

    g_rx_head = 0u;
    g_rx_tail = 0u;
    g_at_resp_len = 0u;
    g_at_waiting = false;
    g_client_count = 0u;
    g_active_link_id = 0u;
    g_active_link_mask = 0u;
    g_status_line_len = 0u;
    g_status_line_buf[0] = '\0';
    g_parse_state = PARSE_IDLE;

    HAL_UART_Receive_IT(&g_esp_uart, &g_rx_byte, 1u);
}

void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&g_esp_uart);
}

void ESP_RxCallback(void)
{
    ESP_RingBuf_Push(g_rx_byte);

    if (g_at_waiting && g_at_resp_len < (ESP_RX_BUF_SIZE - 1u))
    {
        g_at_resp_buf[g_at_resp_len++] = g_rx_byte;
        g_at_resp_buf[g_at_resp_len] = '\0';
    }

    HAL_UART_Receive_IT(&g_esp_uart, &g_rx_byte, 1u);
}

void ESP_ErrorCallback(UART_HandleTypeDef *huart)
{
    if ((huart == NULL) || (huart->Instance != ESP_UART))
    {
        return;
    }

    ESP_UART_Recover();
}

static void ESP_RingBuf_Push(uint8_t byte)
{
    uint16_t next = (uint16_t)((g_rx_head + 1u) % ESP_RX_BUF_SIZE);

    if (next != g_rx_tail)
    {
        g_rx_buf[g_rx_head] = byte;
        g_rx_head = next;
    }
}

static bool ESP_RingBuf_Pop(uint8_t *byte)
{
    if (g_rx_tail == g_rx_head)
    {
        return false;
    }

    *byte = g_rx_buf[g_rx_tail];
    g_rx_tail = (uint16_t)((g_rx_tail + 1u) % ESP_RX_BUF_SIZE);
    return true;
}

static void ESP_UART_Recover(void)
{
    __HAL_UART_DISABLE_IT(&g_esp_uart, UART_IT_RXNE);
    __HAL_UART_DISABLE_IT(&g_esp_uart, UART_IT_PE);
    __HAL_UART_DISABLE_IT(&g_esp_uart, UART_IT_ERR);

    __HAL_UART_CLEAR_PEFLAG(&g_esp_uart);
    __HAL_UART_CLEAR_FEFLAG(&g_esp_uart);
    __HAL_UART_CLEAR_NEFLAG(&g_esp_uart);
    __HAL_UART_CLEAR_OREFLAG(&g_esp_uart);

    g_rx_head = 0u;
    g_rx_tail = 0u;
    g_at_resp_len = 0u;
    g_at_waiting = false;
    g_status_line_len = 0u;
    g_status_line_buf[0] = '\0';
    g_parse_state = PARSE_IDLE;

    g_esp_uart.ErrorCode = HAL_UART_ERROR_NONE;
    g_esp_uart.RxState = HAL_UART_STATE_READY;
    HAL_UART_Receive_IT(&g_esp_uart, &g_rx_byte, 1u);
}

static void ESP_UpdateConnectionState(void)
{
    if (g_connection_state == ESP_CONN_INIT_FAIL)
    {
        return;
    }

    if (g_client_count > 0u)
    {
        g_connection_state = ESP_CONN_CLIENT_CONNECTED;
    }
    else
    {
        g_connection_state = ESP_CONN_AP_READY;
    }
}

static void ESP_RefreshActiveLink(void)
{
    uint8_t link_id;

    for (link_id = 0u; link_id < ESP_MAX_LINKS; link_id++)
    {
        if ((g_active_link_mask & (uint8_t)(1u << link_id)) != 0u)
        {
            g_active_link_id = link_id;
            return;
        }
    }

    g_active_link_id = 0u;
}

static void ESP_MarkLinkConnected(uint8_t link_id)
{
    uint8_t link_bit;

    if (link_id >= ESP_MAX_LINKS)
    {
        return;
    }

    link_bit = (uint8_t)(1u << link_id);
    if ((g_active_link_mask & link_bit) == 0u)
    {
        g_active_link_mask |= link_bit;
        g_client_count++;
    }

    g_active_link_id = link_id;
    ESP_UpdateConnectionState();
}

static void ESP_MarkLinkClosed(uint8_t link_id)
{
    uint8_t link_bit;

    if (link_id >= ESP_MAX_LINKS)
    {
        return;
    }

    link_bit = (uint8_t)(1u << link_id);
    if ((g_active_link_mask & link_bit) != 0u)
    {
        g_active_link_mask &= (uint8_t)(~link_bit);
        if (g_client_count > 0u)
        {
            g_client_count--;
        }
    }

    if ((g_active_link_mask & (uint8_t)(1u << g_active_link_id)) == 0u)
    {
        ESP_RefreshActiveLink();
    }

    ESP_UpdateConnectionState();
}

static void ESP_HandleStatusLine(const char *line)
{
    uint8_t link_id = 0u;

    if ((line == NULL) || (line[0] == '\0'))
    {
        return;
    }

    if (ESP_ParseLinkEvent(line, ",CONNECT", &link_id) != 0u)
    {
        ESP_MarkLinkConnected(link_id);
        return;
    }

    if (ESP_ParseLinkEvent(line, ",CLOSED", &link_id) != 0u)
    {
        ESP_MarkLinkClosed(link_id);
    }
}

static uint8_t ESP_ParseLinkEvent(const char *line, const char *suffix, uint8_t *link_id)
{
    uint16_t value = 0u;
    uint16_t index = 0u;

    if ((line == NULL) || (suffix == NULL) || (link_id == NULL))
    {
        return 0u;
    }

    while ((line[index] >= '0') && (line[index] <= '9'))
    {
        value = (uint16_t)(value * 10u + (uint16_t)(line[index] - '0'));
        index++;
    }

    if ((index == 0u) || (strcmp(&line[index], suffix) != 0))
    {
        return 0u;
    }

    *link_id = (uint8_t)value;
    return 1u;
}

static void ESP_ParseStatusLineChar(uint8_t ch)
{
    if ((ch == '\r') || (ch == '\n'))
    {
        if (g_status_line_len > 0u)
        {
            g_status_line_buf[g_status_line_len] = '\0';
            ESP_HandleStatusLine(g_status_line_buf);
            g_status_line_len = 0u;
            g_status_line_buf[0] = '\0';
        }

        return;
    }

    if ((ch < 0x20u) || (ch > 0x7Eu))
    {
        return;
    }

    if (g_status_line_len < (sizeof(g_status_line_buf) - 1u))
    {
        g_status_line_buf[g_status_line_len++] = (char)ch;
    }
}

ESP_Status_t ESP_SendAT(const char *cmd, const char *ack, uint32_t timeout)
{
    uint32_t start = 0u;

    g_at_resp_len = 0u;
    memset((void *)g_at_resp_buf, 0, sizeof(g_at_resp_buf));
    g_at_waiting = true;

    HAL_UART_Transmit(&g_esp_uart, (uint8_t *)cmd, (uint16_t)strlen(cmd), 1000u);
    HAL_UART_Transmit(&g_esp_uart, (uint8_t *)"\r\n", 2u, 100u);

    start = HAL_GetTick();
    while ((HAL_GetTick() - start) < timeout)
    {
        if (strstr((const char *)g_at_resp_buf, ack) != NULL)
        {
            g_at_waiting = false;
            return ESP_OK;
        }

        if (strstr((const char *)g_at_resp_buf, "ERROR") != NULL)
        {
            g_at_waiting = false;
            return ESP_ERR_AT_FAIL;
        }

        ESP_Delay(10u);
    }

    g_at_waiting = false;
    return ESP_ERR_TIMEOUT;
}

ESP_Status_t ESP_Init(void)
{
    char cmd_buf[128];

    g_connection_state = ESP_CONN_INIT_FAIL;
    g_client_count = 0u;
    g_active_link_id = 0u;
    g_active_link_mask = 0u;
    g_status_line_len = 0u;
    g_status_line_buf[0] = '\0';

    ESP_UART_Init();
    ESP_Delay(1000u);

    if (ESP_SendAT("AT", "OK", ESP_AT_TIMEOUT) != ESP_OK)
    {
        ESP_SendAT("AT+RST", "ready", 5000u);
        ESP_Delay(2000u);

        if (ESP_SendAT("AT", "OK", ESP_AT_TIMEOUT) != ESP_OK)
        {
            return ESP_ERR_AT_FAIL;
        }
    }

    ESP_SendAT("ATE0", "OK", ESP_AT_TIMEOUT);

    if (ESP_SendAT("AT+CWMODE=2", "OK", ESP_AT_TIMEOUT) != ESP_OK)
    {
        return ESP_ERR_AT_FAIL;
    }

    snprintf(cmd_buf, sizeof(cmd_buf), "AT+CWSAP=\"%s\",\"%s\",1,4", ESP_WIFI_SSID, ESP_WIFI_PASS);
    if (ESP_SendAT(cmd_buf, "OK", ESP_AT_TIMEOUT) != ESP_OK)
    {
        return ESP_ERR_AT_FAIL;
    }

    if (ESP_SendAT("AT+CIPMUX=1", "OK", ESP_AT_TIMEOUT) != ESP_OK)
    {
        return ESP_ERR_AT_FAIL;
    }

    snprintf(cmd_buf, sizeof(cmd_buf), "AT+CIPSERVER=1,%s", ESP_SERVER_PORT);
    if (ESP_SendAT(cmd_buf, "OK", ESP_AT_TIMEOUT) != ESP_OK)
    {
        return ESP_ERR_AT_FAIL;
    }

    ESP_SendAT("AT+CIPSTO=180", "OK", ESP_AT_TIMEOUT);

    g_connection_state = ESP_CONN_AP_READY;
    return ESP_OK;
}

ESP_Status_t ESP_SendData(uint8_t link_id, const uint8_t *data, uint16_t len)
{
    char cmd_buf[64];
    uint32_t start = 0u;

    if ((data == NULL) || (len == 0u) || (len > ESP_TX_BUF_SIZE))
    {
        return ESP_ERR_SEND_FAIL;
    }

    snprintf(cmd_buf, sizeof(cmd_buf), "AT+CIPSEND=%u,%u", link_id, len);
    if (ESP_SendAT(cmd_buf, ">", 2000u) != ESP_OK)
    {
        return ESP_ERR_SEND_FAIL;
    }

    g_at_resp_len = 0u;
    memset((void *)g_at_resp_buf, 0, sizeof(g_at_resp_buf));
    g_at_waiting = true;

    HAL_UART_Transmit(&g_esp_uart, (uint8_t *)data, len, 4000u);

    start = HAL_GetTick();
    while ((HAL_GetTick() - start) < 3000u)
    {
        if (strstr((const char *)g_at_resp_buf, "SEND OK") != NULL)
        {
            g_at_waiting = false;
            return ESP_OK;
        }

        if (strstr((const char *)g_at_resp_buf, "SEND FAIL") != NULL)
        {
            g_at_waiting = false;
            return ESP_ERR_SEND_FAIL;
        }

        ESP_Delay(5u);
    }

    g_at_waiting = false;
    return ESP_ERR_TIMEOUT;
}

ESP_Status_t ESP_SendString(uint8_t link_id, const char *str)
{
    return ESP_SendData(link_id, (const uint8_t *)str, (uint16_t)strlen(str));
}

ESP_Status_t ESP_CloseLink(uint8_t link_id)
{
    char cmd_buf[32];

    snprintf(cmd_buf, sizeof(cmd_buf), "AT+CIPCLOSE=%u", link_id);
    return ESP_SendAT(cmd_buf, "OK", 1000u);
}

static void ESP_ParseIPD(void)
{
    uint8_t ch = 0u;

    while (ESP_RingBuf_Pop(&ch))
    {
        if (g_at_waiting)
        {
            continue;
        }

        ESP_ParseStatusLineChar(ch);

        switch (g_parse_state)
        {
        case PARSE_IDLE:
            if (ch == '+')
            {
                g_parse_state = PARSE_PLUS;
                g_header_index = 0u;
            }
            break;

        case PARSE_PLUS:
            g_header_buf[g_header_index++] = (char)ch;
            if (g_header_index == 4u)
            {
                if (memcmp(g_header_buf, "IPD,", 4u) == 0)
                {
                    g_parse_state = PARSE_LINK_ID;
                    g_parse_link_id = 0u;
                }
                else
                {
                    g_parse_state = PARSE_IDLE;
                }
            }
            break;

        case PARSE_LINK_ID:
            if (ch == ',')
            {
                g_parse_state = PARSE_LENGTH;
                g_parse_data_len = 0u;
            }
            else if ((ch >= '0') && (ch <= '9'))
            {
                g_parse_link_id = (uint8_t)(g_parse_link_id * 10u + (uint8_t)(ch - '0'));
            }
            else
            {
                g_parse_state = PARSE_IDLE;
            }
            break;

        case PARSE_LENGTH:
            if (ch == ':')
            {
                g_parse_state = PARSE_DATA;
                g_parse_data_count = 0u;
                if (g_parse_data_len > ESP_DATA_BUF_SIZE)
                {
                    g_parse_data_len = ESP_DATA_BUF_SIZE;
                }
            }
            else if ((ch >= '0') && (ch <= '9'))
            {
                g_parse_data_len = (uint16_t)(g_parse_data_len * 10u + (uint16_t)(ch - '0'));
            }
            else
            {
                g_parse_state = PARSE_IDLE;
            }
            break;

        case PARSE_DATA:
            if (g_parse_data_count < g_parse_data_len)
            {
                g_parse_packet.data[g_parse_data_count++] = ch;
            }

            if (g_parse_data_count >= g_parse_data_len)
            {
                g_parse_packet.len = g_parse_data_len;
                g_parse_packet.link_id = g_parse_link_id;

                if (g_user_callback != NULL)
                {
                    g_user_callback(&g_parse_packet);
                }

                g_parse_state = PARSE_IDLE;
            }
            break;

        default:
            g_parse_state = PARSE_IDLE;
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
    g_user_callback = cb;
}

ESP_ConnectionState_t ESP_GetConnectionState(void)
{
    return g_connection_state;
}

uint8_t ESP_GetActiveLinkId(void)
{
    return g_active_link_id;
}

uint8_t ESP_GetActiveLinkMask(void)
{
    return g_active_link_mask;
}

uint8_t ESP_IsLinkActive(uint8_t link_id)
{
    if (link_id >= ESP_MAX_LINKS)
    {
        return 0u;
    }

    return (uint8_t)((g_active_link_mask & (uint8_t)(1u << link_id)) != 0u);
}

uint8_t ESP_GetClientCount(void)
{
    return g_client_count;
}

static void ESP_Delay(uint32_t ms)
{
    HAL_Delay(ms);
}
