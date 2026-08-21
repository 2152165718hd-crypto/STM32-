#ifndef __BUZZER_H
#define __BUZZER_H

#include "stm32f1xx_hal.h"        // 根据你的芯片型号修改
#include ".\SYSTEM\delay\delay.h" // 包含延时函数头文件

/* ==================== 引脚定义 ==================== */
#define BUZZER_GPIO_PORT GPIOA
#define BUZZER_GPIO_PIN GPIO_PIN_6

/* 函数原型
 * 提供：
 * - 一个初始化函数 `Buzzer_Init()`，返回是否成功（1 成功，0 失败）
 * - 两个控制函数：`Buzzer_On()` 和 `Buzzer_Off()`
 */

void Buzzer_Init(void);

void Buzzer_On(void);
void Buzzer_Off(void);

#endif /* __BUZZER_H */
