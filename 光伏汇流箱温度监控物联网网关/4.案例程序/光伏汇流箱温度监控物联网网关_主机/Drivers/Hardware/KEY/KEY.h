#ifndef __KEY_H
#define __KEY_H

#include "stm32f1xx_hal.h"

/* ============ 按键引脚定义（根据你的接线修改）============ */
#define KEY_UP_PORT GPIOB
#define KEY_UP_PIN GPIO_PIN_12

#define KEY_DOWN_PORT GPIOB
#define KEY_DOWN_PIN GPIO_PIN_13

#define KEY_ENTER_PORT GPIOB
#define KEY_ENTER_PIN GPIO_PIN_14

#define KEY_BACK_PORT GPIOB
#define KEY_BACK_PIN GPIO_PIN_15

/* ============ 输入电平配置 ============
 * 默认上拉输入，按下为低电平。
 * 若使用下拉电路，请改为：
 * KEY_GPIO_PULL = GPIO_PULLDOWN
 * KEY_PRESSED_LEVEL = GPIO_PIN_SET
 */
#ifndef KEY_GPIO_PULL
#define KEY_GPIO_PULL GPIO_PULLUP
#endif

#ifndef KEY_PRESSED_LEVEL
#define KEY_PRESSED_LEVEL GPIO_PIN_RESET
#endif

/* ============ 可选：自动释放 JTAG 占用引脚 ============
 * 当按键使用 PA15/PB3/PB4 时，默认自动执行 NOJTAG 重映射（保留 SWD）。
 */
#ifndef KEY_AUTO_DISABLE_JTAG_FOR_SWJ_PINS
#define KEY_AUTO_DISABLE_JTAG_FOR_SWJ_PINS 1u
#endif

/* ============ 按键扫描定时器配置 ============ */
#ifndef KEY_SCAN_TIM_INSTANCE
#define KEY_SCAN_TIM_INSTANCE TIM2
#endif

#ifndef KEY_SCAN_TIM_CLK_ENABLE
#define KEY_SCAN_TIM_CLK_ENABLE() __HAL_RCC_TIM2_CLK_ENABLE()
#endif

#ifndef KEY_SCAN_TIM_IRQn
#define KEY_SCAN_TIM_IRQn TIM2_IRQn
#endif

#ifndef KEY_SCAN_TIM_IRQHandler
#define KEY_SCAN_TIM_IRQHandler TIM2_IRQHandler
#endif

#ifndef KEY_SCAN_TIM_PRESCALER
#define KEY_SCAN_TIM_PRESCALER 7199u
#endif

#ifndef KEY_SCAN_TIM_PERIOD
#define KEY_SCAN_TIM_PERIOD 99u
#endif

#ifndef KEY_SCAN_TIM_IRQ_PREEMPT_PRIO
#define KEY_SCAN_TIM_IRQ_PREEMPT_PRIO 1u
#endif

#ifndef KEY_SCAN_TIM_IRQ_SUB_PRIO
#define KEY_SCAN_TIM_IRQ_SUB_PRIO 0u
#endif

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
