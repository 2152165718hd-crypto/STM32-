#ifndef __VIBRATOR_H
#define __VIBRATOR_H

#include "stm32f1xx_hal.h"        // 根据你的芯片型号修改
#include ".\SYSTEM\delay\delay.h" // 包含延时函数头文件

/* ==================== 引脚定义 ==================== */
#define VIBRATOR_GPIO_PORT GPIOA
#define VIBRATOR_GPIO_PIN GPIO_PIN_5

/* 函数原型
 * 提供：
 * - 一个初始化函数 `Vibrator_Init()`，返回是否成功（1 成功，0 失败）
 * - 两个控制函数：`Vibrator_On()` 和 `Vibrator_Off()`
 * - 高电平控制震动器
 */

void Vibrator_Init(void);

void Vibrator_On(void);
void Vibrator_Off(void);

#endif /* __VIBRATOR_H */
