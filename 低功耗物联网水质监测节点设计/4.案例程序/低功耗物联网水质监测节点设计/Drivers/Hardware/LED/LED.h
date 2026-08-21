#ifndef __LED_H
#define __LED_H

#include "stm32f1xx_hal.h"

#define LED_GPIO_PORT GPIOA
#define LED_GPIO_PIN  GPIO_PIN_5

void LED_Init(void);
void LED_On(void);
void LED_Off(void);
void LED_Toggle(void);
void LED_Set(uint8_t on);

#endif
