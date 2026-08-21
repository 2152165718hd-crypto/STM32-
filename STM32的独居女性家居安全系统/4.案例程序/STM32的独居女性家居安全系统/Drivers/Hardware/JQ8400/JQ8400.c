// JQ8400.c
#include ".\Hardware\JQ8400\JQ8400.h"

static UART_HandleTypeDef huart1;

/* 内部发送函数 */
static void JQ8400_SendCommand(uint8_t cmd, uint8_t data_len, const uint8_t *data)
{
    uint8_t frame[260];
    uint16_t checksum = 0;
    uint8_t i;

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

    /* 发送完整帧，超时1000ms */
    HAL_UART_Transmit(&huart1, frame, 4 + data_len, 1000);

    /* 指令间隔建议10ms以上 */
    HAL_Delay(20);
}

void JQ8400_Init(void)
{
    /* 使能时钟 */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();

    /* GPIO 配置 */
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_9; // PA9 TX
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_10; // PA10 RX
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* UART 配置 */
    huart1.Instance = USART1;
    huart1.Init.BaudRate = 9600;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&huart1) != HAL_OK)
    {
        /* 初始化失败处理 */
        while (1)
            ;
    }
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

