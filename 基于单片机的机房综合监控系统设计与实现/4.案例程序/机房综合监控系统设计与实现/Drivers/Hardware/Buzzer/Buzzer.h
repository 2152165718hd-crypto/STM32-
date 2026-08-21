#ifndef __BUZZER_H
#define __BUZZER_H

/**
 * @file Buzzer.h
 * @brief 蜂鸣器驱动接口声明。
 */

#include "stm32f1xx_hal.h"
#include ".\SYSTEM\delay\delay.h"

/* ==================== 引脚定义 ==================== */
#define BUZZER_GPIO_PORT GPIOA
#define BUZZER_GPIO_PIN GPIO_PIN_7

/**
 * @brief 初始化蜂鸣器 GPIO。
 */
void Buzzer_Init(void);

/**
 * @brief 打开蜂鸣器。
 */
void Buzzer_On(void);

/**
 * @brief 关闭蜂鸣器。
 */
void Buzzer_Off(void);

#endif /* __BUZZER_H */
