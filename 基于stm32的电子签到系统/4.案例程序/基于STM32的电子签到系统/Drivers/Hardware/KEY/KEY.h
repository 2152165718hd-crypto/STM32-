#ifndef __KEY_H
#define __KEY_H

#include "stm32f1xx_hal.h"

#define KEY_UP_PORT GPIOA
#define KEY_UP_PIN GPIO_PIN_15

#define KEY_DOWN_PORT GPIOB
#define KEY_DOWN_PIN GPIO_PIN_4

#define KEY_ENTER_PORT GPIOB
#define KEY_ENTER_PIN GPIO_PIN_8

#define KEY_BACK_PORT GPIOB
#define KEY_BACK_PIN GPIO_PIN_9

#define KEY_MASK_UP (1u << 0)
#define KEY_MASK_DOWN (1u << 1)
#define KEY_MASK_ENTER (1u << 2)
#define KEY_MASK_BACK (1u << 3)

typedef enum
{
    KEY_NONE = 0,
    KEY_UP,
    KEY_DOWN,
    KEY_ENTER,
    KEY_BACK
} KeyEvent_t;

void Key_Init(void);
KeyEvent_t Key_Scan(void);
uint8_t Key_GetStableMask(void);
void Key_ClearEvents(void);
void Key_TimerCallback(TIM_HandleTypeDef *htim);

#endif
