#ifndef __MIST_MODULE_H
#define __MIST_MODULE_H

#include "stm32f1xx_hal.h"

#define MIST_GPIO_PORT       GPIOA
#define MIST_GPIO_PIN        GPIO_PIN_5

void Mist_Module_Init(void);
void Mist_Module_On(void);
void Mist_Module_Off(void);
void Mist_Module_Toggle(void);

#endif
