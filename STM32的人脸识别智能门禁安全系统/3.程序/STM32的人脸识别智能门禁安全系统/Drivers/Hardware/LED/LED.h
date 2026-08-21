#ifndef __LED_H__
#define __LED_H__

#include "stm32f1xx_hal.h"

#define FILLLIGHT_LED_GPIO_PORT GPIOB
#define FILLLIGHT_LED_PIN GPIO_PIN_11

#define RED_LED_GPIO_PORT GPIOB
#define RED_LED_PIN GPIO_PIN_1

#define GREEN_LED_GPIO_PORT GPIOB
#define GREEN_LED_PIN GPIO_PIN_10



void LED_Init(void);
void LED_On(GPIO_TypeDef *LED_PORT, uint16_t led_pin);
void LED_Off(GPIO_TypeDef *LED_PORT,uint16_t led_pin);
void LED_Toggle(GPIO_TypeDef *LED_PORT,uint16_t led_pin);
void LED_Set(GPIO_TypeDef *LED_PORT,uint16_t led_pin,int8_t on);
#endif
