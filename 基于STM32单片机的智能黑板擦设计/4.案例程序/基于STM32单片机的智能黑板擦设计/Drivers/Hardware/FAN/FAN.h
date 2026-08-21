#ifndef __FAN_H
#define __FAN_H

#include "stm32f1xx_hal.h"

#define FAN_GPIO_PORT        GPIOA
#define FAN_GPIO_PIN         GPIO_PIN_4

void FAN_Init(void);
void FAN_On(void);
void FAN_Off(void);
void FAN_Toggle(void);

#endif
