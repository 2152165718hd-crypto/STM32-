#ifndef __LIGHT_SENSOR_H__
#define __LIGHT_SENSOR_H__

#include "stm32f1xx_hal.h"
#include <stdint.h>

#ifndef LIGHT_ANALOG_INVERT
#define LIGHT_ANALOG_INVERT 1u
#endif

HAL_StatusTypeDef Light_Init(void);
uint16_t Light_ReadRaw(void);
uint8_t Light_ReadPercent(void);
uint8_t Light_ConvertRawToPercent(uint16_t raw);
GPIO_PinState Light_ReadDigital(void);

#endif
