#ifndef __LED_H
#define __LED_H

#include "stm32f1xx_hal.h" // 根据你的芯片型号修改

/* ---- 引脚定义 ---- */
#define LED_GPIO_PORT GPIOA
#define LED_PIN GPIO_PIN_12

/* 若开发板 LED 为高电平点亮，可改为 GPIO_PIN_SET */
#define LED_ON_LEVEL GPIO_PIN_RESET
#define LED_OFF_LEVEL GPIO_PIN_SET

void LED_Init(void);
void LED_On(void);
void LED_Off(void);
void LED_Toggle(void);
uint8_t LED_IsOn(void);

#endif /* __LED_H */