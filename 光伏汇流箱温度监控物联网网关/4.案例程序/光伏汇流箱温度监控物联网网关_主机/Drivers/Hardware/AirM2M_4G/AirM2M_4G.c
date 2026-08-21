#include ".\Hardware\AirM2M_4G\AirM2M_4G.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    uint8_t waiting;
    const char *expect1;
    const char *expect2;
    AirM2M_4G_Result_t result;
} AirM2M_4G_WaitContext_t;

static UART_HandleTypeDef s_air_uart;
static uint8_t s_air_inited = 0U;
static volatile uint16_t s_rx_head = 0U;
static volatile uint16_t s_rx_tail = 0U;
static uint8_t s_rx_ring[AIRM2M_4G_RX_BUFFER_SIZE];
static uint8_t s_rx_byte = 0U;

static char s_line_buf[AIRM2M_4G_LINE_BUFFER_SIZE];
static uint16_t s_line_len = 0U;
static char s_mqtt_topic[AIRM2M_4G_TOPIC_BUFFER_SIZE];
static char s_mqtt_payload[AIRM2M_4G_PAYLOAD_BUFFER_SIZE];
static char s_publish_payload[AIRM2M_4G_PAYLOAD_BUFFER_SIZE];
static char s_at_cmd[AIRM2M_4G_LINE_BUFFER_SIZE];
static char s_last_line[AIRM2M_4G_LINE_BUFFER_SIZE];
static uint32_t s_last_line_tick = 0U;
static char *s_debug_response = NULL;
static uint16_t s_debug_response_size = 0U;

static AirM2M_4G_Status_t s_status = AIRM2M_4G_STATUS_RESET;
static uint8_t s_mqtt_connected = 0U;
static uint8_t s_subscribed = 0U;
static AirM2M_4G_WaitContext_t s_wait = {0};
static AirM2M_4G_MqttMessageCallback_t s_mqtt_message_cb = NULL;
static AirM2M_4G_WaitCallback_t s_wait_callback = NULL;

static void AirM2M_4G_ClearUartError(void)
{
    if (s_air_uart.Instance == NULL)
    {
        return;
    }

    __HAL_UART_CLEAR_PEFLAG(&s_air_uart);
    s_air_uart.ErrorCode = HAL_UART_ERROR_NONE;
}

static void AirM2M_4G_StartRxIT(uint8_t clear_error)
{
    if ((s_air_inited == 0U) || (s_air_uart.Instance == NULL))
    {
        return;
    }

    if (s_air_uart.RxState != HAL_UART_STATE_BUSY_RX)
    {
        if (clear_error != 0U)
        {
            AirM2M_4G_ClearUartError();
        }
        (void)HAL_UART_Receive_IT(&s_air_uart, &s_rx_byte, 1U);
    }
}

static void AirM2M_4G_RunWaitCallback(void)
{
    if (s_wait_callback != NULL)
    {
        s_wait_callback();
    }
}

static void AirM2M_4G_CooperativeDelay(uint32_t delay_ms)
{
    uint32_t start = HAL_GetTick();

    while ((HAL_GetTick() - start) < delay_ms)
    {
        AirM2M_4G_Process();
        AirM2M_4G_RunWaitCallback();
        HAL_Delay(1U);
    }
}

static void AirM2M_4G_GPIOClockEnable(GPIO_TypeDef *port)
{
    if (port == GPIOA)
    {
        __HAL_RCC_GPIOA_CLK_ENABLE();
    }
    else if (port == GPIOB)
    {
        __HAL_RCC_GPIOB_CLK_ENABLE();
    }
    else if (port == GPIOC)
    {
        __HAL_RCC_GPIOC_CLK_ENABLE();
    }
#if defined(GPIOD)
    else if (port == GPIOD)
    {
        __HAL_RCC_GPIOD_CLK_ENABLE();
    }
#endif
#if defined(GPIOE)
    else if (port == GPIOE)
    {
        __HAL_RCC_GPIOE_CLK_ENABLE();
    }
#endif
}

static void AirM2M_4G_USARTClockEnable(void)
{
#if defined(USART1)
    if (AIRM2M_4G_USART == USART1)
    {
        __HAL_RCC_USART1_CLK_ENABLE();
    }
    else
#endif
#if defined(USART2)
        if (AIRM2M_4G_USART == USART2)
    {
        __HAL_RCC_USART2_CLK_ENABLE();
    }
    else
#endif
#if defined(USART3)
        if (AIRM2M_4G_USART == USART3)
    {
        __HAL_RCC_USART3_CLK_ENABLE();
    }
    else
#endif
    {
    }
}

static void AirM2M_4G_RXPush(uint8_t data)
{
    uint16_t next = (uint16_t)((s_rx_head + 1U) % AIRM2M_4G_RX_BUFFER_SIZE);

    if (next != s_rx_tail)
    {
        s_rx_ring[s_rx_head] = data;
        s_rx_head = next;
    }
}

static uint8_t AirM2M_4G_RXPop(uint8_t *data)
{
    if ((data == NULL) || (s_rx_tail == s_rx_head))
    {
        return 0U;
    }

    *data = s_rx_ring[s_rx_tail];
    s_rx_tail = (uint16_t)((s_rx_tail + 1U) % AIRM2M_4G_RX_BUFFER_SIZE);
    return 1U;
}

static void AirM2M_4G_ResetRxParser(void)
{
    s_line_len = 0U;
    memset(s_line_buf, 0, sizeof(s_line_buf));
}

static void AirM2M_4G_CompleteWait(AirM2M_4G_Result_t result)
{
    if (s_wait.waiting != 0U)
    {
        s_wait.result = result;
        s_wait.waiting = 0U;
    }
}

static uint8_t AirM2M_4G_LineMatches(const char *line, const char *expect)
{
    if ((line == NULL) || (expect == NULL))
    {
        return 0U;
    }

    return (strstr(line, expect) != NULL) ? 1U : 0U;
}

static void AirM2M_4G_CaptureDebugLine(const char *line)
{
    if ((s_debug_response == NULL) || (s_debug_response_size == 0U) ||
        (line == NULL) || (line[0] == '\0'))
    {
        return;
    }

    if ((strcmp(line, "OK") == 0) && (s_debug_response[0] != '\0'))
    {
        return;
    }

    memset(s_debug_response, 0, s_debug_response_size);
    strncpy(s_debug_response, line, s_debug_response_size - 1U);
}

static void AirM2M_4G_ParseMqttStatus(const char *line)
{
    const char *colon = NULL;

    if (line == NULL)
    {
        return;
    }

    colon = strchr(line, ':');
    if (colon == NULL)
    {
        return;
    }

    while ((*colon == ':') || (*colon == ' '))
    {
        colon++;
    }

    if (*colon == '1')
    {
        s_mqtt_connected = 1U;
        if (s_status < AIRM2M_4G_STATUS_MQTT_CONNECTED)
        {
            s_status = AIRM2M_4G_STATUS_MQTT_CONNECTED;
        }
    }
    else if (*colon == '0')
    {
        s_mqtt_connected = 0U;
        s_subscribed = 0U;
        s_status = AIRM2M_4G_STATUS_CLOSED;
    }
    else
    {
        s_mqtt_connected = 0U;
        s_subscribed = 0U;
        s_status = AIRM2M_4G_STATUS_CLOSED;
    }
}

static void AirM2M_4G_HandleMqttMessage(const char *line)
{
    const char *topic_start = NULL;
    const char *topic_end = NULL;
    const char *len_start = NULL;
    const char *byte_mark = NULL;
    const char *payload_start = NULL;
    uint32_t payload_len = 0U;
    uint16_t topic_len = 0U;
    uint16_t copy_len = 0U;

    if ((line == NULL) || (s_mqtt_message_cb == NULL))
    {
        return;
    }

    topic_start = strchr(line, '"');
    if (topic_start == NULL)
    {
        return;
    }
    topic_start++;

    topic_end = strchr(topic_start, '"');
    if (topic_end == NULL)
    {
        return;
    }

    topic_len = (uint16_t)(topic_end - topic_start);
    if (topic_len >= sizeof(s_mqtt_topic))
    {
        topic_len = (uint16_t)(sizeof(s_mqtt_topic) - 1U);
    }

    memset(s_mqtt_topic, 0, sizeof(s_mqtt_topic));
    memcpy(s_mqtt_topic, topic_start, topic_len);

    len_start = strchr(topic_end + 1, ',');
    if (len_start == NULL)
    {
        return;
    }
    len_start++;

    payload_len = (uint32_t)strtoul(len_start, NULL, 10);
    byte_mark = strstr(len_start, "byte,");
    if (byte_mark == NULL)
    {
        byte_mark = strstr(len_start, "BYTE,");
    }
    if (byte_mark == NULL)
    {
        return;
    }

    payload_start = byte_mark + 5;
    if (payload_len == 0U)
    {
        payload_len = (uint32_t)strlen(payload_start);
    }
    else if (payload_len > (uint32_t)strlen(payload_start))
    {
        payload_len = (uint32_t)strlen(payload_start);
    }

    copy_len = (payload_len >= sizeof(s_mqtt_payload)) ? (uint16_t)(sizeof(s_mqtt_payload) - 1U) : (uint16_t)payload_len;
    memset(s_mqtt_payload, 0, sizeof(s_mqtt_payload));
    memcpy(s_mqtt_payload, payload_start, copy_len);

    s_mqtt_message_cb(s_mqtt_topic, s_mqtt_payload, copy_len);
}

static void AirM2M_4G_HandleLine(const char *line)
{
    if ((line == NULL) || (line[0] == '\0'))
    {
        return;
    }

    memset(s_last_line, 0, sizeof(s_last_line));
    strncpy(s_last_line, line, sizeof(s_last_line) - 1U);
    s_last_line_tick = HAL_GetTick();
    AirM2M_4G_CaptureDebugLine(line);

    if ((strcmp(line, "ERROR") == 0) || (strstr(line, "+CME ERROR") != NULL))
    {
        s_status = AIRM2M_4G_STATUS_ERROR;
        AirM2M_4G_CompleteWait(AIRM2M_4G_ERROR);
        return;
    }

    if ((strcmp(line, "CLOSED") == 0) ||
        (strstr(line, "CLOSE OK") != NULL) ||
        (strstr(line, "DISCONNECT") != NULL) ||
        (strstr(line, "PDP DEACT") != NULL))
    {
        s_mqtt_connected = 0U;
        s_subscribed = 0U;
        s_status = AIRM2M_4G_STATUS_CLOSED;
        if (strcmp(line, "CLOSED") == 0)
        {
            AirM2M_4G_CompleteWait(AIRM2M_4G_ERROR);
            return;
        }
    }

    if ((strstr(line, "CONNECT FAIL") != NULL) ||
        (strstr(line, "CONNECT ERROR") != NULL) ||
        (strstr(line, "CONNACK FAIL") != NULL) ||
        (strstr(line, "CONNACK ERROR") != NULL))
    {
        s_mqtt_connected = 0U;
        s_subscribed = 0U;
        s_status = AIRM2M_4G_STATUS_ERROR;
        AirM2M_4G_CompleteWait(AIRM2M_4G_ERROR);
        return;
    }

    if (strstr(line, "+MQTTSTATU") != NULL)
    {
        AirM2M_4G_ParseMqttStatus(line);
    }

    if (strstr(line, "+MSUB:") == line)
    {
        AirM2M_4G_HandleMqttMessage(line);
    }

    if (strstr(line, "CONNECT OK") != NULL)
    {
        s_status = AIRM2M_4G_STATUS_TCP_CONNECTED;
    }
    else if (strstr(line, "CONNACK OK") != NULL)
    {
        s_mqtt_connected = 1U;
        s_status = AIRM2M_4G_STATUS_MQTT_CONNECTED;
    }
    else if (strstr(line, "SUBACK") != NULL)
    {
        s_subscribed = 1U;
        s_status = AIRM2M_4G_STATUS_SUBSCRIBED;
    }

    if (s_wait.waiting != 0U)
    {
        if ((s_wait.expect1 != NULL) &&
            (AirM2M_4G_LineMatches(line, s_wait.expect1) != 0U))
        {
            AirM2M_4G_CompleteWait(AIRM2M_4G_OK);
        }
        else if ((s_wait.expect2 != NULL) &&
                 (AirM2M_4G_LineMatches(line, s_wait.expect2) != 0U))
        {
            AirM2M_4G_CompleteWait(AIRM2M_4G_OK);
        }
        else if ((s_wait.expect1 == NULL) && (strcmp(line, "OK") == 0))
        {
            AirM2M_4G_CompleteWait(AIRM2M_4G_OK);
        }
    }
}

void AirM2M_4G_Process(void)
{
    uint8_t data = 0U;

    while (AirM2M_4G_RXPop(&data) != 0U)
    {
        if ((data == '\r') || (data == '\n'))
        {
            if (s_line_len > 0U)
            {
                s_line_buf[s_line_len] = '\0';
                AirM2M_4G_HandleLine(s_line_buf);
                AirM2M_4G_ResetRxParser();
            }
        }
        else
        {
            if (s_line_len < (AIRM2M_4G_LINE_BUFFER_SIZE - 1U))
            {
                s_line_buf[s_line_len++] = (char)data;
            }
            else
            {
                AirM2M_4G_ResetRxParser();
            }
        }
    }
}

static AirM2M_4G_Result_t AirM2M_4G_SendRaw(const char *data)
{
    uint16_t len = 0U;

    if ((s_air_inited == 0U) || (data == NULL))
    {
        return AIRM2M_4G_PARAM;
    }

    len = (uint16_t)strlen(data);
    if (HAL_UART_Transmit(&s_air_uart, (uint8_t *)data, len, AIRM2M_4G_AT_TIMEOUT_MS) != HAL_OK)
    {
        return AIRM2M_4G_ERROR;
    }

    return AIRM2M_4G_OK;
}

static AirM2M_4G_Result_t AirM2M_4G_SendCommand(const char *cmd,
                                                 const char *expect1,
                                                 const char *expect2,
                                                 uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    AirM2M_4G_Result_t tx_result;

    if ((cmd == NULL) || (timeout_ms == 0U))
    {
        return AIRM2M_4G_PARAM;
    }

    AirM2M_4G_StartRxIT(1U);

    if (s_wait.waiting != 0U)
    {
        return AIRM2M_4G_BUSY;
    }

    s_wait.expect1 = expect1;
    s_wait.expect2 = expect2;
    s_wait.result = AIRM2M_4G_TIMEOUT;
    s_wait.waiting = 1U;

    tx_result = AirM2M_4G_SendRaw(cmd);
    if (tx_result == AIRM2M_4G_OK)
    {
        tx_result = AirM2M_4G_SendRaw("\r\n");
    }
    if (tx_result != AIRM2M_4G_OK)
    {
        s_wait.waiting = 0U;
        return tx_result;
    }

    while ((HAL_GetTick() - start) < timeout_ms)
    {
        AirM2M_4G_Process();
        AirM2M_4G_RunWaitCallback();
        if (s_wait.waiting == 0U)
        {
            return s_wait.result;
        }
        HAL_Delay(1U);
    }

    s_wait.waiting = 0U;
    return AIRM2M_4G_TIMEOUT;
}

static uint8_t AirM2M_4G_EscapePayload(const char *payload, char *out, uint16_t out_size)
{
    uint16_t pos = 0U;

    if ((payload == NULL) || (out == NULL) || (out_size == 0U))
    {
        return 0U;
    }

    while (*payload != '\0')
    {
        if ((*payload == '"') || (*payload == '\\'))
        {
            if ((pos + 3U) >= out_size)
            {
                return 0U;
            }
            out[pos++] = '\\';
            if (*payload == '"')
            {
                out[pos++] = '2';
                out[pos++] = '2';
            }
            else
            {
                out[pos++] = '5';
                out[pos++] = 'C';
            }
        }
        else
        {
            if ((pos + 1U) >= out_size)
            {
                return 0U;
            }
            out[pos++] = *payload;
        }
        payload++;
    }

    out[pos] = '\0';
    return 1U;
}

static uint8_t AirM2M_4G_IsIpv4String(const char *text)
{
    uint8_t dot_count = 0U;
    uint8_t digit_count = 0U;
    uint16_t value = 0U;

    if ((text == NULL) || (text[0] == '\0'))
    {
        return 0U;
    }

    while (*text == ' ')
    {
        text++;
    }

    while (*text != '\0')
    {
        if ((*text >= '0') && (*text <= '9'))
        {
            value = (uint16_t)(value * 10U + (uint16_t)(*text - '0'));
            if (value > 255U)
            {
                return 0U;
            }
            digit_count++;
        }
        else if (*text == '.')
        {
            if ((digit_count == 0U) || (dot_count >= 3U))
            {
                return 0U;
            }
            dot_count++;
            digit_count = 0U;
            value = 0U;
        }
        else if (*text == ' ')
        {
            break;
        }
        else
        {
            return 0U;
        }
        text++;
    }

    return ((dot_count == 3U) && (digit_count > 0U)) ? 1U : 0U;
}

static AirM2M_4G_Result_t AirM2M_4G_QueryIpAddress(char *ip_addr, uint16_t ip_addr_size)
{
    AirM2M_4G_Result_t result;

    if ((ip_addr == NULL) || (ip_addr_size == 0U))
    {
        return AIRM2M_4G_PARAM;
    }

    ip_addr[0] = '\0';
    result = AirM2M_4G_DebugCommand("AT+CIFSR",
                                    ".",
                                    NULL,
                                    AIRM2M_4G_AT_TIMEOUT_MS,
                                    ip_addr,
                                    ip_addr_size);
    if ((result == AIRM2M_4G_OK) && (AirM2M_4G_IsIpv4String(ip_addr) != 0U))
    {
        return AIRM2M_4G_OK;
    }

    return AIRM2M_4G_ERROR;
}

static AirM2M_4G_Result_t AirM2M_4G_WaitForATReady(void)
{
    uint8_t retry;
    AirM2M_4G_Result_t result = AIRM2M_4G_TIMEOUT;

    for (retry = 0U; retry < 5U; retry++)
    {
        result = AirM2M_4G_SendCommand("AT", NULL, NULL, AIRM2M_4G_AT_TIMEOUT_MS);
        if (result == AIRM2M_4G_OK)
        {
            s_status = AIRM2M_4G_STATUS_AT_READY;
            return result;
        }
        AirM2M_4G_CooperativeDelay(200U);
    }

    return result;
}

AirM2M_4G_Result_t AirM2M_4G_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    HAL_NVIC_DisableIRQ(AIRM2M_4G_IRQ);
    if (s_air_inited != 0U)
    {
        (void)HAL_UART_AbortReceive(&s_air_uart);
    }
    memset(&s_air_uart, 0, sizeof(s_air_uart));

    __HAL_RCC_AFIO_CLK_ENABLE();
    AirM2M_4G_GPIOClockEnable(AIRM2M_4G_UART_TX_PORT);
    AirM2M_4G_GPIOClockEnable(AIRM2M_4G_UART_RX_PORT);
    AirM2M_4G_USARTClockEnable();
    AIRM2M_4G_USART_REMAP_ENABLE();

    GPIO_InitStruct.Pin = AIRM2M_4G_UART_TX_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(AIRM2M_4G_UART_TX_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = AIRM2M_4G_UART_RX_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(AIRM2M_4G_UART_RX_PORT, &GPIO_InitStruct);

    s_air_uart.Instance = AIRM2M_4G_USART;
    s_air_uart.Init.BaudRate = AIRM2M_4G_BAUDRATE;
    s_air_uart.Init.WordLength = UART_WORDLENGTH_8B;
    s_air_uart.Init.StopBits = UART_STOPBITS_1;
    s_air_uart.Init.Parity = UART_PARITY_NONE;
    s_air_uart.Init.Mode = UART_MODE_TX_RX;
    s_air_uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    s_air_uart.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&s_air_uart) != HAL_OK)
    {
        s_air_inited = 0U;
        s_status = AIRM2M_4G_STATUS_ERROR;
        return AIRM2M_4G_ERROR;
    }

    s_rx_head = 0U;
    s_rx_tail = 0U;
    AirM2M_4G_ResetRxParser();
    memset(s_last_line, 0, sizeof(s_last_line));
    s_last_line_tick = 0U;
    s_wait.waiting = 0U;
    s_wait.result = AIRM2M_4G_OK;
    s_mqtt_connected = 0U;
    s_subscribed = 0U;
    s_status = AIRM2M_4G_STATUS_RESET;
    s_air_inited = 1U;
    AirM2M_4G_ClearUartError();

    HAL_NVIC_SetPriority(AIRM2M_4G_IRQ, 1U, 0U);
    HAL_NVIC_EnableIRQ(AIRM2M_4G_IRQ);

    if (HAL_UART_Receive_IT(&s_air_uart, &s_rx_byte, 1U) != HAL_OK)
    {
        s_air_inited = 0U;
        s_status = AIRM2M_4G_STATUS_ERROR;
        return AIRM2M_4G_ERROR;
    }

    if (AirM2M_4G_WaitForATReady() != AIRM2M_4G_OK)
    {
        return AIRM2M_4G_TIMEOUT;
    }

    (void)AirM2M_4G_SendCommand("ATE0", NULL, NULL, AIRM2M_4G_AT_TIMEOUT_MS);
    return AIRM2M_4G_OK;
}

AirM2M_4G_Result_t AirM2M_4G_CheckNetwork(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    AirM2M_4G_Result_t result;
    char ip_addr[32];
    int written;

    if (s_air_inited == 0U)
    {
        return AIRM2M_4G_NOT_CONNECTED;
    }

    result = AirM2M_4G_SendCommand("AT+CPIN?", "+CPIN: READY", NULL, AIRM2M_4G_AT_TIMEOUT_MS);
    if (result != AIRM2M_4G_OK)
    {
        return result;
    }
    s_status = AIRM2M_4G_STATUS_SIM_READY;

    (void)AirM2M_4G_SendCommand("AT+CSQ", NULL, NULL, AIRM2M_4G_AT_TIMEOUT_MS);

    while ((HAL_GetTick() - start) < timeout_ms)
    {
        result = AirM2M_4G_SendCommand("AT+CGATT?", "+CGATT: 1", NULL, AIRM2M_4G_AT_TIMEOUT_MS);
        if (result == AIRM2M_4G_OK)
        {
            written = snprintf(s_at_cmd,
                               sizeof(s_at_cmd),
                               "AT+CSTT=\"%s\",\"%s\",\"%s\"",
                               AIRM2M_4G_APN,
                               AIRM2M_4G_APN_USERNAME,
                               AIRM2M_4G_APN_PASSWORD);
            if ((written > 0) && ((uint32_t)written < sizeof(s_at_cmd)))
            {
                result = AirM2M_4G_SendCommand(s_at_cmd, NULL, NULL, AIRM2M_4G_AT_TIMEOUT_MS);
            }
            else
            {
                result = AIRM2M_4G_PARAM;
            }

            if (result != AIRM2M_4G_OK)
            {
                result = AirM2M_4G_SendCommand("AT+CSTT", NULL, NULL, AIRM2M_4G_AT_TIMEOUT_MS);
            }

            if ((result == AIRM2M_4G_OK) || (result == AIRM2M_4G_ERROR))
            {
                result = AirM2M_4G_SendCommand("AT+CIICR", NULL, NULL, AIRM2M_4G_NET_TIMEOUT_MS);
                if ((result == AIRM2M_4G_OK) ||
                    (AirM2M_4G_QueryIpAddress(ip_addr, (uint16_t)sizeof(ip_addr)) == AIRM2M_4G_OK))
                {
                    s_status = AIRM2M_4G_STATUS_NET_READY;
                    return AIRM2M_4G_OK;
                }
            }
        }
        AirM2M_4G_CooperativeDelay(1000U);
    }

    return AIRM2M_4G_TIMEOUT;
}

static void AirM2M_4G_CloseMqttSession(uint8_t keep_network_ready)
{
    if (s_air_inited == 0U)
    {
        return;
    }

    (void)AirM2M_4G_SendCommand("AT+MDISCONNECT", "OK", "DISCONNECT OK", AIRM2M_4G_CLOSE_TIMEOUT_MS);
    AirM2M_4G_CooperativeDelay(100U);
    (void)AirM2M_4G_SendCommand("AT+MIPCLOSE", "OK", "CLOSE OK", AIRM2M_4G_CLOSE_TIMEOUT_MS);

    s_mqtt_connected = 0U;
    s_subscribed = 0U;
    s_status = (keep_network_ready != 0U) ? AIRM2M_4G_STATUS_NET_READY : AIRM2M_4G_STATUS_CLOSED;
}

AirM2M_4G_Result_t AirM2M_4G_MqttConnect(const char *host,
                                          uint16_t port,
                                          const char *client_id,
                                          const char *username,
                                          const char *password,
                                          uint16_t keepalive,
                                          uint8_t clean_session)
{
    int written;
    AirM2M_4G_Result_t result;

    if ((host == NULL) || (client_id == NULL) || (username == NULL) || (password == NULL) ||
        (host[0] == '\0') || (client_id[0] == '\0') || (username[0] == '\0') || (password[0] == '\0'))
    {
        return AIRM2M_4G_PARAM;
    }

    result = AirM2M_4G_CheckNetwork(AIRM2M_4G_NET_TIMEOUT_MS);
    if (result != AIRM2M_4G_OK)
    {
        return result;
    }
    AirM2M_4G_CloseMqttSession(1U);

    written = snprintf(s_at_cmd, sizeof(s_at_cmd), "AT+MCONFIG=\"%s\",\"%s\",\"%s\"", client_id, username, password);
    if ((written < 0) || ((uint32_t)written >= sizeof(s_at_cmd)))
    {
        return AIRM2M_4G_PARAM;
    }
    result = AirM2M_4G_SendCommand(s_at_cmd, NULL, NULL, AIRM2M_4G_AT_TIMEOUT_MS);
    if (result != AIRM2M_4G_OK)
    {
        return result;
    }

    (void)AirM2M_4G_SendCommand("AT+MQTTMODE=0", NULL, NULL, AIRM2M_4G_AT_TIMEOUT_MS);

    written = snprintf(s_at_cmd, sizeof(s_at_cmd), "AT+MIPSTART=\"%s\",%u", host, (unsigned int)port);
    if ((written < 0) || ((uint32_t)written >= sizeof(s_at_cmd)))
    {
        return AIRM2M_4G_PARAM;
    }
    result = AirM2M_4G_SendCommand(s_at_cmd, "CONNECT OK", "ALREADY CONNECT", AIRM2M_4G_MQTT_TIMEOUT_MS);
    if (result != AIRM2M_4G_OK)
    {
        AirM2M_4G_CloseMqttSession(1U);
        written = snprintf(s_at_cmd, sizeof(s_at_cmd), "AT+MIPSTART=\"%s\",\"%u\"", host, (unsigned int)port);
        if ((written < 0) || ((uint32_t)written >= sizeof(s_at_cmd)))
        {
            return AIRM2M_4G_PARAM;
        }
        result = AirM2M_4G_SendCommand(s_at_cmd, "CONNECT OK", "ALREADY CONNECT", AIRM2M_4G_MQTT_TIMEOUT_MS);
    }
    if (result != AIRM2M_4G_OK)
    {
        AirM2M_4G_CloseMqttSession(0U);
        return result;
    }

    written = snprintf(s_at_cmd,
                       sizeof(s_at_cmd),
                       "AT+MCONNECT=%u,%u",
                       (unsigned int)((clean_session != 0U) ? 1U : 0U),
                       (unsigned int)keepalive);
    if ((written < 0) || ((uint32_t)written >= sizeof(s_at_cmd)))
    {
        return AIRM2M_4G_PARAM;
    }

    result = AirM2M_4G_SendCommand(s_at_cmd, "CONNACK OK", NULL, AIRM2M_4G_MQTT_TIMEOUT_MS);
    if (result == AIRM2M_4G_OK)
    {
        (void)AirM2M_4G_SendCommand("AT+MQTTMSGSET=0", NULL, NULL, AIRM2M_4G_AT_TIMEOUT_MS);
        s_mqtt_connected = 1U;
        s_subscribed = 0U;
        s_status = AIRM2M_4G_STATUS_MQTT_CONNECTED;
    }
    else
    {
        AirM2M_4G_CloseMqttSession(0U);
    }
    return result;
}

AirM2M_4G_Result_t AirM2M_4G_MqttSubscribe(const char *topic, uint8_t qos)
{
    int written;
    AirM2M_4G_Result_t result;

    if ((topic == NULL) || (topic[0] == '\0') || (qos > 2U))
    {
        return AIRM2M_4G_PARAM;
    }
    if (s_mqtt_connected == 0U)
    {
        return AIRM2M_4G_NOT_CONNECTED;
    }

    written = snprintf(s_at_cmd, sizeof(s_at_cmd), "AT+MSUB=\"%s\",%u", topic, (unsigned int)qos);
    if ((written < 0) || ((uint32_t)written >= sizeof(s_at_cmd)))
    {
        return AIRM2M_4G_PARAM;
    }

    result = AirM2M_4G_SendCommand(s_at_cmd, "SUBACK", NULL, AIRM2M_4G_MQTT_TIMEOUT_MS);
    if (result == AIRM2M_4G_OK)
    {
        s_subscribed = 1U;
        s_status = AIRM2M_4G_STATUS_SUBSCRIBED;
    }
    return result;
}

AirM2M_4G_Result_t AirM2M_4G_MqttPublish(const char *topic,
                                          const char *payload,
                                          uint8_t qos,
                                          uint8_t retain)
{
    int written;

    if ((topic == NULL) || (payload == NULL) || (topic[0] == '\0') || (qos > 2U))
    {
        return AIRM2M_4G_PARAM;
    }
    if (s_mqtt_connected == 0U)
    {
        return AIRM2M_4G_NOT_CONNECTED;
    }
    if (AirM2M_4G_EscapePayload(payload, s_publish_payload, (uint16_t)sizeof(s_publish_payload)) == 0U)
    {
        return AIRM2M_4G_PARAM;
    }

    written = snprintf(s_at_cmd,
                       sizeof(s_at_cmd),
                       "AT+MPUB=\"%s\",%u,%u,\"%s\"",
                       topic,
                       (unsigned int)qos,
                       (unsigned int)((retain != 0U) ? 1U : 0U),
                       s_publish_payload);
    if ((written < 0) || ((uint32_t)written >= sizeof(s_at_cmd)))
    {
        return AIRM2M_4G_PARAM;
    }

    return AirM2M_4G_SendCommand(s_at_cmd, NULL, NULL, AIRM2M_4G_PUBLISH_TIMEOUT_MS);
}

AirM2M_4G_Result_t AirM2M_4G_QueryMqttStatus(void)
{
    if (s_air_inited == 0U)
    {
        return AIRM2M_4G_NOT_CONNECTED;
    }

    return AirM2M_4G_SendCommand("AT+MQTTSTATU", "+MQTTSTATU", NULL, AIRM2M_4G_AT_TIMEOUT_MS);
}

AirM2M_4G_Result_t AirM2M_4G_DebugCommand(const char *cmd,
                                           const char *expect1,
                                           const char *expect2,
                                           uint32_t timeout_ms,
                                           char *response,
                                           uint16_t response_size)
{
    AirM2M_4G_Result_t result;
    uint32_t start_tick;

    if ((cmd == NULL) || (timeout_ms == 0U))
    {
        return AIRM2M_4G_PARAM;
    }

    if ((response != NULL) && (response_size > 0U))
    {
        response[0] = '\0';
    }

    AirM2M_4G_Process();
    memset(s_last_line, 0, sizeof(s_last_line));
    s_last_line_tick = 0U;
    start_tick = HAL_GetTick();

    s_debug_response = response;
    s_debug_response_size = response_size;
    result = AirM2M_4G_SendCommand(cmd, expect1, expect2, timeout_ms);
    s_debug_response = NULL;
    s_debug_response_size = 0U;

    if ((response != NULL) && (response_size > 0U) &&
        (response[0] == '\0') && (s_last_line[0] != '\0') &&
        ((s_last_line_tick - start_tick) <= timeout_ms))
    {
        strncpy(response, s_last_line, response_size - 1U);
        response[response_size - 1U] = '\0';
    }

    return result;
}

void AirM2M_4G_RegisterMqttMessageCallback(AirM2M_4G_MqttMessageCallback_t cb)
{
    s_mqtt_message_cb = cb;
}

void AirM2M_4G_RegisterWaitCallback(AirM2M_4G_WaitCallback_t cb)
{
    s_wait_callback = cb;
}

AirM2M_4G_Status_t AirM2M_4G_GetStatus(void)
{
    return s_status;
}

const char *AirM2M_4G_GetStatusText(void)
{
    switch (s_status)
    {
    case AIRM2M_4G_STATUS_RESET:
        return "RESET";
    case AIRM2M_4G_STATUS_AT_READY:
        return "AT";
    case AIRM2M_4G_STATUS_SIM_READY:
        return "SIM";
    case AIRM2M_4G_STATUS_NET_READY:
        return "NET";
    case AIRM2M_4G_STATUS_TCP_CONNECTED:
        return "TCP";
    case AIRM2M_4G_STATUS_MQTT_CONNECTED:
        return "MQTT";
    case AIRM2M_4G_STATUS_SUBSCRIBED:
        return "SUB";
    case AIRM2M_4G_STATUS_CLOSED:
        return "CLOSE";
    case AIRM2M_4G_STATUS_ERROR:
        return "ERR";
    default:
        return "UNKN";
    }
}

const char *AirM2M_4G_GetLastLine(void)
{
    return s_last_line;
}

uint32_t AirM2M_4G_GetLastLineTick(void)
{
    return s_last_line_tick;
}

uint8_t AirM2M_4G_IsMqttConnected(void)
{
    return s_mqtt_connected;
}

uint8_t AirM2M_4G_IsSubscribed(void)
{
    return s_subscribed;
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if ((huart != NULL) && (huart->Instance == AIRM2M_4G_USART))
    {
        AirM2M_4G_RXPush(s_rx_byte);
        AirM2M_4G_StartRxIT(0U);
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if ((huart != NULL) && (huart->Instance == AIRM2M_4G_USART))
    {
        AirM2M_4G_ClearUartError();
        AirM2M_4G_StartRxIT(0U);
    }
}

void AIRM2M_4G_IRQ_HANDLER(void)
{
    HAL_UART_IRQHandler(&s_air_uart);
}
