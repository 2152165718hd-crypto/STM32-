#ifndef __FLAME_H__
#define __FLAME_H__

#include "stm32f1xx_hal.h"

/* ==================== 引脚定义 ==================== */
#define FLAME_DO_PORT GPIOA
#define FLAME_DO_PIN GPIO_PIN_1

#define FLAME_AO_PORT GPIOA
#define FLAME_AO_PIN GPIO_PIN_0

#define FLAME_CHANNEL ADC_CHANNEL_0

/* ==================== 函数声明 ==================== */
void Flame_Init(void);
uint8_t Flame_GetDO(void);
uint16_t Flame_GetAO_Raw(void);
float Flame_GetVoltage(void);
uint8_t Flame_GetAO_Percent(void);

 #endif /* __FLAME_H__ */
