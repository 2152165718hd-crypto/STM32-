#include ".\Hardware\LightSensor\LightSensor.h"
#include <string.h>

static UART_HandleTypeDef s_light_uart;
static uint8_t s_light_online = 0U;

static void LightSensor_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_USART3_CLK_ENABLE();

    GPIO_InitStruct.Pin = LIGHT_SENSOR_UART_TX_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(LIGHT_SENSOR_UART_TX_GPIO_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = LIGHT_SENSOR_UART_RX_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(LIGHT_SENSOR_UART_RX_GPIO_PORT, &GPIO_InitStruct);
}

void LightSensor_Init(void)
{
    memset(&s_light_uart, 0, sizeof(s_light_uart));
    s_light_online = 0U;

    LightSensor_GPIO_Init();

    s_light_uart.Instance = LIGHT_SENSOR_UART;
    s_light_uart.Init.BaudRate = LIGHT_SENSOR_UART_BAUDRATE;
    s_light_uart.Init.WordLength = UART_WORDLENGTH_8B;
    s_light_uart.Init.StopBits = UART_STOPBITS_1;
    s_light_uart.Init.Parity = UART_PARITY_NONE;
    s_light_uart.Init.Mode = UART_MODE_TX_RX;
    s_light_uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    s_light_uart.Init.OverSampling = UART_OVERSAMPLING_16;
    (void)HAL_UART_Init(&s_light_uart);
    __HAL_UART_FLUSH_DRREGISTER(&s_light_uart);
}

uint16_t LightSensor_Crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFFU;
    uint8_t bit;

    if (data == NULL)
    {
        return 0U;
    }

    while (len-- != 0U)
    {
        crc ^= *data++;
        for (bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 0x0001U) != 0U)
            {
                crc >>= 1U;
                crc ^= 0xA001U;
            }
            else
            {
                crc >>= 1U;
            }
        }
    }

    return crc;
}

LightSensor_Result_t LightSensor_ReadLux(float *lux)
{
    uint8_t tx[8];
    uint8_t rx[9];
    uint16_t crc;
    uint32_t raw;

    if (lux == NULL)
    {
        return LIGHT_SENSOR_PARAM;
    }

    tx[0] = LIGHT_SENSOR_DEVICE_ADDR;
    tx[1] = 0x03U;
    tx[2] = 0x00U;
    tx[3] = 0x02U;
    tx[4] = 0x00U;
    tx[5] = 0x02U;
    crc = LightSensor_Crc16(tx, 6U);
    tx[6] = (uint8_t)(crc & 0x00FFU);
    tx[7] = (uint8_t)((crc >> 8U) & 0x00FFU);

    if (HAL_UART_Transmit(&s_light_uart, tx, sizeof(tx), LIGHT_SENSOR_TIMEOUT_MS) != HAL_OK)
    {
        s_light_online = 0U;
        return LIGHT_SENSOR_ERROR;
    }

    memset(rx, 0, sizeof(rx));
    if (HAL_UART_Receive(&s_light_uart, rx, sizeof(rx), LIGHT_SENSOR_TIMEOUT_MS) != HAL_OK)
    {
        s_light_online = 0U;
        return LIGHT_SENSOR_TIMEOUT;
    }

    if ((rx[0] != LIGHT_SENSOR_DEVICE_ADDR) || (rx[1] != 0x03U) || (rx[2] != 0x04U))
    {
        s_light_online = 0U;
        return LIGHT_SENSOR_FRAME_ERROR;
    }

    crc = LightSensor_Crc16(rx, 7U);
    if ((rx[7] != (uint8_t)(crc & 0x00FFU)) || (rx[8] != (uint8_t)((crc >> 8U) & 0x00FFU)))
    {
        s_light_online = 0U;
        return LIGHT_SENSOR_CRC_ERROR;
    }

    raw = ((uint32_t)rx[3] << 24U) |
          ((uint32_t)rx[4] << 16U) |
          ((uint32_t)rx[5] << 8U) |
          (uint32_t)rx[6];
    *lux = (float)raw / 1000.0f;
    s_light_online = 1U;

    return LIGHT_SENSOR_OK;
}

uint8_t LightSensor_IsOnline(void)
{
    return s_light_online;
}
