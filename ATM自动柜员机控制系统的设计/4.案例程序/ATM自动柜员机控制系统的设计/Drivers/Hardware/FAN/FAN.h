#ifndef __FAN_H
#define __FAN_H

#include "stm32f1xx_hal.h"

#define FAN_GPIO_PORT GPIOA
#define FAN_GPIO_PIN  GPIO_PIN_2
#define FAN_ACTIVE_LEVEL   GPIO_PIN_RESET
#define FAN_INACTIVE_LEVEL GPIO_PIN_SET

void FAN_Init(void);
void FAN_Set(uint8_t on);
uint8_t FAN_GetState(void);

#endif /* __FAN_H */
