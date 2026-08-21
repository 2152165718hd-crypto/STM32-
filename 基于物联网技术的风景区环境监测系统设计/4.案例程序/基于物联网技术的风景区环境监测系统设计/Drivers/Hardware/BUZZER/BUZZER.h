#ifndef __BUZZER_H
#define __BUZZER_H

#include "stm32f1xx_hal.h"

/* 有源蜂鸣器：PB0，低电平触发 */
#define BUZZER_PORT GPIOB
#define BUZZER_PIN  GPIO_PIN_0

void Buzzer_Init(void);
void Buzzer_On(void);
void Buzzer_Off(void);
void Buzzer_Toggle(void);
void Buzzer_Beep(uint16_t ms);

#endif
