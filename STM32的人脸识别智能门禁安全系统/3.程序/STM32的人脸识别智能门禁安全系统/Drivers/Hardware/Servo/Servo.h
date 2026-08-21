#ifndef __SERVO_H__
#define __SERVO_H__

#include "stm32f1xx_hal.h"
#define SERVO_GPIO_PORT GPIOA
#define SERVO_PIN GPIO_PIN_6

void Servo_Init(void);
void Servo_SetAngle(uint8_t angle);

#endif
