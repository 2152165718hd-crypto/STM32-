#ifndef __KEY_H
#define __KEY_H

/**
 * @file KEY.h
 * @brief 按键扫描驱动接口声明。
 */

#include "stm32f1xx_hal.h"

/* ============ 按键引脚定义（根据你的接线修改）============ */
#define KEY_UP_PORT GPIOB
#define KEY_UP_PIN GPIO_PIN_13

#define KEY_DOWN_PORT GPIOB
#define KEY_DOWN_PIN GPIO_PIN_12

#define KEY_ENTER_PORT GPIOB
#define KEY_ENTER_PIN GPIO_PIN_14

#define KEY_BACK_PORT GPIOB
#define KEY_BACK_PIN GPIO_PIN_15

/* ============ 按键事件定义 ============ */
/** @brief 按键事件枚举。 */
typedef enum
{
    KEY_NONE = 0,
    KEY_UP,
    KEY_DOWN,
    KEY_ENTER,
    KEY_BACK
} KeyEvent_t;

/* ============ 函数声明 ============ */
/**
 * @brief 初始化按键 GPIO 与定时扫描中断。
 */
void Key_Init(void);

/**
 * @brief 读取一个去抖后的按键事件。
 * @return 若队列为空则返回 `KEY_NONE`。
 */
KeyEvent_t Key_Scan(void);

#endif /* __KEY_H */
