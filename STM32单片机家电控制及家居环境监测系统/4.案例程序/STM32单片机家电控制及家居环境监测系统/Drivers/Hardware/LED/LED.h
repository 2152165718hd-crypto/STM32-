#ifndef __LED_H__
#define __LED_H__

#include "stm32f1xx_hal.h"

#define ROOM1_LED_GPIO_PORT GPIOA
#define ROOM1_LED_PIN GPIO_PIN_8

#define ROOM2_LED_GPIO_PORT GPIOA
#define ROOM2_LED_PIN GPIO_PIN_11

#define ROOM3_LED_GPIO_PORT GPIOA
#define ROOM3_LED_PIN GPIO_PIN_12

#define WASHROOM_LED_GPIO_PORT GPIOB 
#define WASHROOM_LED_PIN GPIO_PIN_5

#define HOST_LED_GPIO_PORT GPIOB
#define HOST_LED_PIN GPIO_PIN_9

#define COOL_LED_GPIO_PORT GPIOB
#define COOL_LED_PIN GPIO_PIN_8

void LED_Init(void);
void LED_On(GPIO_TypeDef *LED_PORT, uint16_t led_pin);
void LED_Off(GPIO_TypeDef *LED_PORT,uint16_t led_pin);
void LED_Toggle(GPIO_TypeDef *LED_PORT,uint16_t led_pin);
void LED_Set(GPIO_TypeDef *LED_PORT,uint16_t led_pin,int8_t on);
#endif
