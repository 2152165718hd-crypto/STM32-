#ifndef __BUZZER_H
#define __BUZZER_H

#include "stm32f4xx_hal.h"

#define BUZZER_GPIO_PORT GPIOA
#define BUZZER_GPIO_PIN GPIO_PIN_3

#ifndef BUZZER_ACTIVE_LEVEL
#define BUZZER_ACTIVE_LEVEL GPIO_PIN_RESET
#endif

void Buzzer_Init(void);
void Buzzer_On(void);
void Buzzer_Off(void);
void Buzzer_Beep(uint16_t ms);

#endif
