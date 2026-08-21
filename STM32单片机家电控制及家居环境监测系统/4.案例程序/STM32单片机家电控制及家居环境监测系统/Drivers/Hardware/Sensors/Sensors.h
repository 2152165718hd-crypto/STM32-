#ifndef __SENSORS_H__
#define __SENSORS_H__

#include "stm32f1xx_hal.h"

#define SOUND_SENSOR_GPIO_PORT GPIOA
#define SOUND_SENSOR_GPIO_PIN GPIO_PIN_0
#define SOUND_SENSOR_ADC_CHANNEL ADC_CHANNEL_0

#define RAIN_SENSOR_GPIO_PORT GPIOA
#define RAIN_SENSOR_GPIO_PIN GPIO_PIN_1
#define RAIN_SENSOR_ADC_CHANNEL ADC_CHANNEL_1

#define LIGHT_SENSOR_GPIO_PORT GPIOA
#define LIGHT_SENSOR_GPIO_PIN GPIO_PIN_2
#define LIGHT_SENSOR_ADC_CHANNEL ADC_CHANNEL_2


void LightSensor_Init(void);
void SoundSensor_Init(void);
void RainSensor_Init(void);

uint16_t LightSensor_ReadAnalog(void);
uint16_t SoundSensor_ReadAnalog(void);
uint16_t RainSensor_ReadAnalog(void);

#endif
