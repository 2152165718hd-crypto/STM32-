#ifndef __DHT11_H
#define __DHT11_H

#include "stm32f1xx_hal.h"
#include ".\SYSTEM\delay\delay.h"

#define DHT11_Data_PORT GPIOB
#define DHT11_Data_PIN GPIO_PIN_1
#define DHT11_Data_CLK_ENABLE() __HAL_RCC_GPIOB_CLK_ENABLE()

typedef struct
{
    uint8_t humi_int;
    uint8_t temp_int;
    uint8_t humidity;
    uint8_t temperature;
} DHT11_Data_t;

extern DHT11_Data_t DHT11_Data;

uint8_t DHT11_Init(void);
uint8_t DHT11_ReadData(void);
uint8_t DHT11_GetTemperature(void);
uint8_t DHT11_GetHumidity(void);

#endif /* __DHT11_H */
