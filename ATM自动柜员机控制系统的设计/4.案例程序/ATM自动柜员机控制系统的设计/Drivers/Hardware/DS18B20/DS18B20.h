#ifndef DS18B20_H
#define DS18B20_H

#include "stm32f1xx_hal.h"

#define DS18B20_PORT GPIOA
#define DS18B20_PIN  GPIO_PIN_1
#define DS18B20_GPIO_CLK_EN() __HAL_RCC_GPIOA_CLK_ENABLE()

#define DS18B20_TEMP_ERROR (-999.0f)

uint8_t DS18B20_Init(void);
uint8_t DS18B20_IsReady(void);
uint8_t DS18B20_StartConversion(void);
float DS18B20_ReadTemperatureResult(void);
float DS18B20_ReadTemperature(void);

#endif /* DS18B20_H */
