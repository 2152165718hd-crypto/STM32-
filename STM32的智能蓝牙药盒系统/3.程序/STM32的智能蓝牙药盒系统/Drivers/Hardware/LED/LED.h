#ifndef __LED_H__
#define __LED_H__

/**
 * @file LED.h
 * @brief 指示灯驱动接口声明。
 */

#include "stm32f1xx_hal.h"

#define MEDICINE_LED_PORT GPIOA
#define MEDICINE_LED_PIN GPIO_PIN_8

/**
 * @brief 初始化药盒指示灯 GPIO。
 */
void LED_Init(void);

/**
 * @brief 点亮指定 LED。
 * @param LED_PORT LED 所在端口。
 * @param led_pin LED 引脚。
 */
void LED_On(GPIO_TypeDef *LED_PORT, uint16_t led_pin);

/**
 * @brief 熄灭指定 LED。
 * @param LED_PORT LED 所在端口。
 * @param led_pin LED 引脚。
 */
void LED_Off(GPIO_TypeDef *LED_PORT, uint16_t led_pin);

/**
 * @brief 翻转指定 LED 状态。
 * @param LED_PORT LED 所在端口。
 * @param led_pin LED 引脚。
 */
void LED_Toggle(GPIO_TypeDef *LED_PORT, uint16_t led_pin);

/**
 * @brief 按布尔值设置指定 LED 状态。
 * @param LED_PORT LED 所在端口。
 * @param led_pin LED 引脚。
 * @param on 非零表示点亮，零表示熄灭。
 */
void LED_Set(GPIO_TypeDef *LED_PORT, uint16_t led_pin, int8_t on);

#endif /* __LED_H__ */
