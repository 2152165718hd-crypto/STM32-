#ifndef __LED_H
#define __LED_H

#include "stm32f1xx_hal.h"

#define LED1_GPIO_PORT GPIOA
#define LED1_GPIO_PIN  GPIO_PIN_11
#define LED2_GPIO_PORT GPIOA
#define LED2_GPIO_PIN  GPIO_PIN_12

#define LED_ON_LEVEL   GPIO_PIN_RESET
#define LED_OFF_LEVEL  GPIO_PIN_SET

typedef enum
{
    LED_GATE = 0,
    LED_ALARM
} LED_Id_t;

void LED_Init(void);
void LED_Set(LED_Id_t led, uint8_t on);
void LED_Toggle(LED_Id_t led);
void LED_AllOff(void);
uint8_t LED_GetState(LED_Id_t led);

#endif /* __LED_H */
