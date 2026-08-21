#ifndef __KEY_H
#define __KEY_H

#include "stm32f1xx_hal.h"

#define KEY_UP_PORT GPIOB
#define KEY_UP_PIN GPIO_PIN_12

#define KEY_DOWN_PORT GPIOB
#define KEY_DOWN_PIN GPIO_PIN_13

#define KEY_ENTER_PORT GPIOB
#define KEY_ENTER_PIN GPIO_PIN_14

#define KEY_BACK_PORT GPIOB
#define KEY_BACK_PIN GPIO_PIN_15

#ifndef KEY_GPIO_PULL
#define KEY_GPIO_PULL GPIO_PULLUP
#endif

#ifndef KEY_PRESSED_LEVEL
#define KEY_PRESSED_LEVEL GPIO_PIN_RESET
#endif

#ifndef KEY_AUTO_DISABLE_JTAG_FOR_SWJ_PINS
#define KEY_AUTO_DISABLE_JTAG_FOR_SWJ_PINS 1u
#endif

#ifndef KEY_SCAN_TIM_INSTANCE
#define KEY_SCAN_TIM_INSTANCE TIM4
#endif

#ifndef KEY_SCAN_TIM_CLK_ENABLE
#define KEY_SCAN_TIM_CLK_ENABLE() __HAL_RCC_TIM4_CLK_ENABLE()
#endif

#ifndef KEY_SCAN_TIM_IRQn
#define KEY_SCAN_TIM_IRQn TIM4_IRQn
#endif

#ifndef KEY_SCAN_TIM_IRQHandler
#define KEY_SCAN_TIM_IRQHandler TIM4_IRQHandler
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

#ifndef KEY_REPEAT_START_TICKS
#define KEY_REPEAT_START_TICKS 40u
#endif

#ifndef KEY_REPEAT_INTERVAL_TICKS
#define KEY_REPEAT_INTERVAL_TICKS 2u
#endif

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
void Key_TimerCallback(TIM_HandleTypeDef *htim);

#endif
