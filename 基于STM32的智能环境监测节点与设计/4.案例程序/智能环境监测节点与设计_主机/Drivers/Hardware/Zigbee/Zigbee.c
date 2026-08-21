#include ".\Hardware\Zigbee\Zigbee.h"
#include <string.h>

static UART_HandleTypeDef s_zigbee_uart;
static EnvProto_Parser_t s_parser;

static volatile uint8_t s_rx_buffer[ZIGBEE_RX_BUFFER_SIZE];
static volatile uint16_t s_rx_head = 0U;
static volatile uint16_t s_rx_tail = 0U;

static EnvProto_Frame_t s_frame_queue[ZIGBEE_FRAME_QUEUE_SIZE];
static volatile uint8_t s_frame_head = 0U;
static volatile uint8_t s_frame_tail = 0U;

static void Zigbee_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    ZIGBEE_UART_TX_CLK_ENABLE();
    ZIGBEE_UART_RX_CLK_ENABLE();

    GPIO_InitStruct.Pin = ZIGBEE_UART_TX_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ZIGBEE_UART_TX_GPIO_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = ZIGBEE_UART_RX_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(ZIGBEE_UART_RX_GPIO_PORT, &GPIO_InitStruct);
}

static void Zigbee_UART_Init(void)
{
    ZIGBEE_UART_CLK_ENABLE();

    s_zigbee_uart.Instance = ZIGBEE_UART;
    s_zigbee_uart.Init.BaudRate = ZIGBEE_UART_BAUDRATE;
    s_zigbee_uart.Init.WordLength = UART_WORDLENGTH_8B;
    s_zigbee_uart.Init.StopBits = UART_STOPBITS_1;
    s_zigbee_uart.Init.Parity = UART_PARITY_NONE;
    s_zigbee_uart.Init.Mode = UART_MODE_TX_RX;
    s_zigbee_uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    s_zigbee_uart.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&s_zigbee_uart);

    __HAL_UART_FLUSH_DRREGISTER(&s_zigbee_uart);
    __HAL_UART_ENABLE_IT(&s_zigbee_uart, UART_IT_RXNE);
    __HAL_UART_ENABLE_IT(&s_zigbee_uart, UART_IT_ERR);

    HAL_NVIC_SetPriority(ZIGBEE_UART_IRQn, 2U, 0U);
    HAL_NVIC_EnableIRQ(ZIGBEE_UART_IRQn);
}

static void Zigbee_PushRxByte(uint8_t byte)
{
    uint16_t next = (uint16_t)((s_rx_tail + 1U) % ZIGBEE_RX_BUFFER_SIZE);

    if (next == s_rx_head)
    {
        s_rx_head = (uint16_t)((s_rx_head + 1U) % ZIGBEE_RX_BUFFER_SIZE);
    }

    s_rx_buffer[s_rx_tail] = byte;
    s_rx_tail = next;
}

static uint8_t Zigbee_PopRxByte(uint8_t *byte)
{
    if ((byte == NULL) || (s_rx_head == s_rx_tail))
    {
        return 0U;
    }

    *byte = s_rx_buffer[s_rx_head];
    s_rx_head = (uint16_t)((s_rx_head + 1U) % ZIGBEE_RX_BUFFER_SIZE);
    return 1U;
}

static void Zigbee_PushFrame(const EnvProto_Frame_t *frame)
{
    uint8_t next = (uint8_t)((s_frame_tail + 1U) % ZIGBEE_FRAME_QUEUE_SIZE);

    if (frame == NULL)
    {
        return;
    }

    if (next == s_frame_head)
    {
        s_frame_head = (uint8_t)((s_frame_head + 1U) % ZIGBEE_FRAME_QUEUE_SIZE);
    }

    s_frame_queue[s_frame_tail] = *frame;
    s_frame_tail = next;
}

void Zigbee_Init(void)
{
    memset((void *)s_rx_buffer, 0, sizeof(s_rx_buffer));
    memset(s_frame_queue, 0, sizeof(s_frame_queue));
    s_rx_head = 0U;
    s_rx_tail = 0U;
    s_frame_head = 0U;
    s_frame_tail = 0U;

    EnvProto_ResetParser(&s_parser);
    Zigbee_GPIO_Init();
    Zigbee_UART_Init();
}

uint8_t Zigbee_SendFrame(const uint8_t *data, uint16_t len)
{
    if ((data == NULL) || (len == 0U))
    {
        return 0U;
    }

    return (HAL_UART_Transmit(&s_zigbee_uart, (uint8_t *)data, len, 100U) == HAL_OK) ? 1U : 0U;
}

void Zigbee_Task(void)
{
    uint8_t byte = 0U;
    EnvProto_Frame_t frame;

    while (Zigbee_PopRxByte(&byte) != 0U)
    {
        if (EnvProto_ParseByte(&s_parser, byte, &frame) != 0U)
        {
            Zigbee_PushFrame(&frame);
        }
    }
}

uint8_t Zigbee_GetFrame(EnvProto_Frame_t *frame)
{
    if ((frame == NULL) || (s_frame_head == s_frame_tail))
    {
        return 0U;
    }

    *frame = s_frame_queue[s_frame_head];
    s_frame_head = (uint8_t)((s_frame_head + 1U) % ZIGBEE_FRAME_QUEUE_SIZE);
    return 1U;
}

void USART2_IRQHandler(void)
{
    if ((__HAL_UART_GET_IT_SOURCE(&s_zigbee_uart, UART_IT_RXNE) != RESET) &&
        (__HAL_UART_GET_FLAG(&s_zigbee_uart, UART_FLAG_RXNE) != RESET))
    {
        uint8_t byte = (uint8_t)(s_zigbee_uart.Instance->DR & 0x00FFU);
        Zigbee_PushRxByte(byte);
    }

    if ((__HAL_UART_GET_FLAG(&s_zigbee_uart, UART_FLAG_ORE) != RESET) ||
        (__HAL_UART_GET_FLAG(&s_zigbee_uart, UART_FLAG_NE) != RESET) ||
        (__HAL_UART_GET_FLAG(&s_zigbee_uart, UART_FLAG_FE) != RESET) ||
        (__HAL_UART_GET_FLAG(&s_zigbee_uart, UART_FLAG_PE) != RESET))
    {
        volatile uint32_t tmpreg = 0U;
        tmpreg = s_zigbee_uart.Instance->SR;
        tmpreg = s_zigbee_uart.Instance->DR;
        (void)tmpreg;
        EnvProto_ResetParser(&s_parser);
    }
}
