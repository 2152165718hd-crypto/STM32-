#ifndef __SERVO_H__
#define __SERVO_H__

/**
 * @file Servo.h
 * @brief 舵机驱动接口声明。
 */

#include "stm32f1xx_hal.h"

#define SERVO_GPIO_PORT GPIOB
#define SERVO_PIN GPIO_PIN_4

/**
 * @brief 初始化舵机 PWM 输出。
 */
void Servo_Init(void);

/**
 * @brief 设置舵机角度。
 * @param angle 目标角度，范围 `0~180`。
 */
void Servo_SetAngle(uint8_t angle);

#endif /* __SERVO_H__ */
