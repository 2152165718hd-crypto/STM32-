#ifndef __LED_H__
#define __LED_H__

#include "stm32f1xx_hal.h"

#define LED_GPIO_PORT GPIOA
#define LED_GPIO_PIN GPIO_PIN_12

void LED_Init(void);
void LED_On(GPIO_TypeDef *LED_PORT, uint16_t led_pin);
void LED_Off(GPIO_TypeDef *LED_PORT,uint16_t led_pin);
void LED_Toggle(GPIO_TypeDef *LED_PORT,uint16_t led_pin);
void LED_Set(GPIO_TypeDef *LED_PORT,uint16_t led_pin,int8_t on);
#endif
