#ifndef __BUZZER_H
#define __BUZZER_H

#include "stm32f1xx_hal.h"

#define BUZZER_GPIO_PORT GPIOA
#define BUZZER_GPIO_PIN  GPIO_PIN_0

#ifndef BUZZER_ACTIVE_LEVEL
#define BUZZER_ACTIVE_LEVEL GPIO_PIN_RESET
#endif

void Buzzer_Init(void);
void Buzzer_Set(uint8_t on);
void Buzzer_On(void);
void Buzzer_Off(void);
uint8_t Buzzer_GetState(void);

#endif /* __BUZZER_H */
