#ifndef __KEY_H
#define __KEY_H

#include "stm32f1xx_hal.h"

/* ============ 按键引脚定义（根据你的接线修改）============ */
#define KEY_UP_PORT GPIOB
#define KEY_UP_PIN GPIO_PIN_8

#define KEY_DOWN_PORT GPIOB
#define KEY_DOWN_PIN GPIO_PIN_9

#define KEY_ENTER_PORT GPIOB
#define KEY_ENTER_PIN GPIO_PIN_4

#define KEY_BACK_PORT GPIOB
#define KEY_BACK_PIN GPIO_PIN_5

/* ============ 按键事件定义 ============ */
typedef enum
{
    KEY_NONE = 0,
    KEY_UP,
    KEY_DOWN,
    KEY_ENTER,
    KEY_BACK
} KeyEvent_t;

/* ============ 函数声明 ============ */
void Key_Init(void);
KeyEvent_t Key_Scan(void);
void Key_TimerCallback(TIM_HandleTypeDef *htim); /* 需在 HAL_TIM_PeriodElapsedCallback 中调用 */

#endif
