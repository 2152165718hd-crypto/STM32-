#ifndef __SR505_H
#define __SR505_H

#include "stm32f1xx_hal.h"

#define SR505_GPIO_PORT GPIOA
#define SR505_GPIO_PIN GPIO_PIN_3

// 红外传感器初始化函数
void SR505_Init(void);

// 返回值：1表示有人经过，0表示无人经过
uint8_t SR505_Read(void);

#endif /* __SR505_H */
