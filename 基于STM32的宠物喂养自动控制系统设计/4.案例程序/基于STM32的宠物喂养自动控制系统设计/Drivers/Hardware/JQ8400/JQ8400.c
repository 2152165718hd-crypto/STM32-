// JQ8400.c
#include ".\Hardware\JQ8400\JQ8400.h"

static UART_HandleTypeDef huart_jq8400;
static uint8_t jq8400_inited = 0U;

/* 根据 GPIO 端口开启对应时钟。 */
static void JQ8400_GPIO_Clock_Enable(GPIO_TypeDef *port)
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

/* 根据所选 UART 开启对应外设时钟。 */
static void JQ8400_USART_Clock_Enable(void)
{
#if defined(USART1)
    if (JQ8400_USART == USART1)
    {
        __HAL_RCC_USART1_CLK_ENABLE();
    }
    else
#endif
#if defined(USART2)
        if (JQ8400_USART == USART2)
    {
        __HAL_RCC_USART2_CLK_ENABLE();
    }
    else
#endif
#if defined(USART3)
        if (JQ8400_USART == USART3)
    {
        __HAL_RCC_USART3_CLK_ENABLE();
    }
    else
#endif
    {
        /* 未覆盖的串口实例，保持空实现。 */
    }
}

/* 内部发送函数 */
static void JQ8400_SendCommand(uint8_t cmd, uint8_t data_len, const uint8_t *data)
{
    uint8_t frame[260];
    uint16_t checksum = 0;
    uint8_t i;

    if (!jq8400_inited)
    {
        return;
    }

    if ((data_len > 0U) && (data == NULL))
    {
        return;
    }

    frame[0] = 0xAA;
    checksum += 0xAA;

    frame[1] = cmd;
    checksum += cmd;

    frame[2] = data_len;
    checksum += data_len;

    for (i = 0; i < data_len; i++)
    {
        frame[3 + i] = data[i];
        checksum += data[i];
    }

    frame[3 + data_len] = (uint8_t)(checksum);

    /* 发送完整帧。 */
    HAL_UART_Transmit(&huart_jq8400, frame, (uint16_t)(4U + data_len), JQ8400_UART_TIMEOUT_MS);

    /* 指令间隔建议10ms以上 */
    HAL_Delay(JQ8400_CMD_INTERVAL_MS);
}

void JQ8400_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_AFIO_CLK_ENABLE();

    JQ8400_GPIO_Clock_Enable(JQ8400_TX_PORT);
    JQ8400_GPIO_Clock_Enable(JQ8400_RX_PORT);
#if (JQ8400_USE_BUSY_PIN)
    JQ8400_GPIO_Clock_Enable(JQ8400_BUSY_PORT);
#endif
    JQ8400_USART_Clock_Enable();
    JQ8400_USART_REMAP_ENABLE();

    /* GPIO 配置 */
    GPIO_InitStruct.Pin = JQ8400_TX_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(JQ8400_TX_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = JQ8400_RX_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(JQ8400_RX_PORT, &GPIO_InitStruct);

#if (JQ8400_USE_BUSY_PIN)
    GPIO_InitStruct.Pin = JQ8400_BUSY_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(JQ8400_BUSY_PORT, &GPIO_InitStruct);
#endif

    /* UART 配置 */
    huart_jq8400.Instance = JQ8400_USART;
    huart_jq8400.Init.BaudRate = JQ8400_BAUDRATE;
    huart_jq8400.Init.WordLength = UART_WORDLENGTH_8B;
    huart_jq8400.Init.StopBits = UART_STOPBITS_1;
    huart_jq8400.Init.Parity = UART_PARITY_NONE;
    huart_jq8400.Init.Mode = UART_MODE_TX_RX;
    huart_jq8400.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart_jq8400.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&huart_jq8400) != HAL_OK)
    {
        jq8400_inited = 0U;
        return;
    }

    jq8400_inited = 1U;
}

uint8_t JQ8400_IsBusy(void)
{
#if (JQ8400_USE_BUSY_PIN)
    return (HAL_GPIO_ReadPin(JQ8400_BUSY_PORT, JQ8400_BUSY_PIN) == JQ8400_BUSY_ACTIVE_LEVEL) ? 1U : 0U;
#else
    return 0U;
#endif
}

void JQ8400_Play(void) { JQ8400_SendCommand(0x02, 0, NULL); }
void JQ8400_Pause(void) { JQ8400_SendCommand(0x03, 0, NULL); }
void JQ8400_Stop(void) { JQ8400_SendCommand(0x04, 0, NULL); }
void JQ8400_Previous(void) { JQ8400_SendCommand(0x05, 0, NULL); }
void JQ8400_Next(void) { JQ8400_SendCommand(0x06, 0, NULL); }
void JQ8400_VolumeUp(void) { JQ8400_SendCommand(0x14, 0, NULL); }
void JQ8400_VolumeDown(void) { JQ8400_SendCommand(0x15, 0, NULL); }

void JQ8400_SetVolume(uint8_t volume)
{
    if (volume > 30)
        volume = 30;
    uint8_t data = volume;
    JQ8400_SendCommand(0x13, 1, &data);
}

void JQ8400_SetEQ(JQ8400_EQ_t eq)
{
    uint8_t data = (uint8_t)eq;
    JQ8400_SendCommand(0x1A, 1, &data);
}

void JQ8400_SetPlayMode(JQ8400_PlayMode_t mode)
{
    uint8_t data = (uint8_t)mode;
    JQ8400_SendCommand(0x18, 1, &data);
}

void JQ8400_SelectDevice(JQ8400_Device_t dev)
{
    uint8_t data = (uint8_t)dev;
    JQ8400_SendCommand(0x0B, 1, &data);
}

void JQ8400_PlayTrack(uint16_t track)
{
    if (track == 0)
        track = 1;
    uint8_t data[2];
    data[0] = (track >> 8) & 0xFF;
    data[1] = track & 0xFF;
    JQ8400_SendCommand(0x07, 2, data);
}

void JQ8400_InsertTrack(JQ8400_Device_t dev, uint16_t track)
{
    if (track == 0)
        track = 1;
    uint8_t data[3];
    data[0] = (uint8_t)dev;
    data[1] = (track >> 8) & 0xFF;
    data[2] = track & 0xFF;
    JQ8400_SendCommand(0x16, 3, data);
}

void JQ8400_EndInsert(void)
{
    JQ8400_SendCommand(0x10, 0, NULL);
}

