#ifndef __STROBE_H__
#define __STROBE_H__

#include "stm32f1xx_hal.h"

#define STROBE_GPIO_PORT GPIOA
#define STROBE_GPIO_PIN GPIO_PIN_6

void Strobe_Init(void);
void Strobe_Blink_Start(uint32_t period_ms);
void Strobe_Blink_Stop(void);
void Strobe_Timer_IRQHandler(void);

#endif
