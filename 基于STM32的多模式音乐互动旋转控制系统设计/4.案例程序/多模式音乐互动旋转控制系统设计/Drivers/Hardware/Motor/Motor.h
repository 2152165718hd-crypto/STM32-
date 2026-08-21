#ifndef __MOTOR_H__
#define __MOTOR_H__

#include <stdint.h>

#include "stm32f1xx_hal.h"

#define MOTOR_PWMB_GPIO_PORT GPIOB
#define MOTOR_PWMB_PIN GPIO_PIN_0

#define MOTOR_PWMA_GPIO_PORT GPIOA
#define MOTOR_PWMA_PIN GPIO_PIN_3

#define MOTOR_IN_GPIO_PORT GPIOA

#define MOTOR_AIN2_PIN GPIO_PIN_4
#define MOTOR_AIN1_PIN GPIO_PIN_5
#define MOTOR_BIN2_PIN GPIO_PIN_7
#define MOTOR_BIN1_PIN GPIO_PIN_6

void Motor_Init(void);
void Motor_SetRightSpeed(int16_t speed_permille);
void Motor_SetLeftSpeed(int16_t speed_permille);
void Motor_SetDutyPermille(uint16_t duty_permille);
uint16_t Motor_GetDutyPermille(void);

void Motor_SetPercent(uint8_t percent);
void Motor_Stop(void);
uint8_t Motor_GetDutyPercent(void);

#endif
