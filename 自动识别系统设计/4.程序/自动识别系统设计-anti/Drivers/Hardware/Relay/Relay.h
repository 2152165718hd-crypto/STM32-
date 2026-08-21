#ifndef __RELAY_H__
#define __RELAY_H__

#include "stm32f1xx_hal.h"
/* 继电器控制引脚定义（根据你的接线修改） */
#define RELAY_PORT GPIOA
#define RELAY_PIN GPIO_PIN_8
void Relay_Init(void);
void Relay_On(void);
void Relay_Off(void);

#endif
