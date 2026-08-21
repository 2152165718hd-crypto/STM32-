#ifndef __WATER_SENSOR_H
#define __WATER_SENSOR_H

#include "stm32f1xx_hal.h"

#define WATER_SENSOR_GPIO_PORT GPIOA
#define WATER_SENSOR_AO_GPIO_PIN GPIO_PIN_5
#define WATER_SENSOR_CHANNEL ADC_CHANNEL_5

#define WATER_SENSOR_ADC_VREF 3.3f
#define WATER_SENSOR_ADC_MAX 4095.0f

void Water_Sensor_Init(void);
uint16_t Water_Sensor_GetAO_Raw(void);
float Water_Sensor_GetVoltage(void);
uint8_t Water_Sensor_GetPercent(void);
uint8_t Water_Sensor_IsWaterDetected(uint16_t threshold);

#endif /* __WATER_SENSOR_H */   