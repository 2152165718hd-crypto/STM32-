#ifndef __L9110H_MOTOR_H
#define __L9110H_MOTOR_H

/**
 * @file L9110H_Motor.h
 * @brief L9110H 电机驱动接口声明。
 */

#include "stm32f1xx_hal.h"

#define MOTOR_INA_PORT GPIOB
#define MOTOR_INA_PIN GPIO_PIN_8
#define MOTOR_INB_PORT GPIOB
#define MOTOR_INB_PIN GPIO_PIN_9

/**
 * @brief 初始化电机驱动引脚。
 */
void Motor_Init(void);

/**
 * @brief 控制电机正转。
 */
void Motor_Forward(void);

/**
 * @brief 控制电机反转。
 */
void Motor_Reverse(void);

/**
 * @brief 停止电机。
 */
void Motor_Stop(void);

#endif /* __L9110H_MOTOR_H */
