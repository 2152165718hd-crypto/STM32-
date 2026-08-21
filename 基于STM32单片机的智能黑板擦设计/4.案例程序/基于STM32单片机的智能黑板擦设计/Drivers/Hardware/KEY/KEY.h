#ifndef __KEY_H
#define __KEY_H

#include "stm32f1xx_hal.h"

/* ============ 按键引脚定义（根据你的接线修改）============ */
#define KEY_UP_PORT GPIOA
#define KEY_UP_PIN GPIO_PIN_8

#define KEY_DOWN_PORT GPIOA
#define KEY_DOWN_PIN GPIO_PIN_9

#define KEY_ENTER_PORT GPIOA
#define KEY_ENTER_PIN GPIO_PIN_10

#define KEY_BACK_PORT GPIOA
#define KEY_BACK_PIN GPIO_PIN_11

#define KEY_MOTOR_PORT GPIOA
#define KEY_MOTOR_PIN GPIO_PIN_12

/* ============ 按键事件定义 ============ */
typedef enum
{
    KEY_NONE = 0,
    KEY_UP,
    KEY_DOWN,
    KEY_ENTER,
    KEY_BACK,
    KEY_MOTOR
} KeyEvent_t;

/* ============ 函数声明 ============ */
void Key_Init(void);
KeyEvent_t Key_Scan(void);
void Key_TimerCallback(TIM_HandleTypeDef *htim); /* 需在 HAL_TIM_PeriodElapsedCallback 中调用 */

#endif
