#ifndef __LIGHT_SENSOR_H__
#define __LIGHT_SENSOR_H__

#include "stm32f1xx_hal.h"
#include <stdint.h>

#define LIGHT_SENSOR_UART                  USART3
#define LIGHT_SENSOR_UART_IRQn             USART3_IRQn
#define LIGHT_SENSOR_UART_BAUDRATE         9600U
#define LIGHT_SENSOR_DEVICE_ADDR           0x01U
#define LIGHT_SENSOR_TIMEOUT_MS            300U

#define LIGHT_SENSOR_UART_TX_PIN           GPIO_PIN_10
#define LIGHT_SENSOR_UART_TX_GPIO_PORT     GPIOB
#define LIGHT_SENSOR_UART_RX_PIN           GPIO_PIN_11
#define LIGHT_SENSOR_UART_RX_GPIO_PORT     GPIOB

typedef enum
{
    LIGHT_SENSOR_OK = 0,
    LIGHT_SENSOR_PARAM,
    LIGHT_SENSOR_TIMEOUT,
    LIGHT_SENSOR_ERROR,
    LIGHT_SENSOR_CRC_ERROR,
    LIGHT_SENSOR_FRAME_ERROR
} LightSensor_Result_t;

void LightSensor_Init(void);
LightSensor_Result_t LightSensor_ReadLux(float *lux);
uint8_t LightSensor_IsOnline(void);
uint16_t LightSensor_Crc16(const uint8_t *data, uint16_t len);

#endif /* __LIGHT_SENSOR_H__ */
