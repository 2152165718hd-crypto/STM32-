#ifndef __KEY_H
#define __KEY_H

#include "stm32f4xx_hal.h"
#include <stdint.h>

/* SW1/SW2/SW3/SW4/SW5/SW6 = up/down/left/right/enter/back. */
#define KEY_UP_PORT GPIOB
#define KEY_UP_PIN GPIO_PIN_7

#define KEY_DOWN_PORT GPIOD
#define KEY_DOWN_PIN GPIO_PIN_4

#define KEY_LEFT_PORT GPIOD
#define KEY_LEFT_PIN GPIO_PIN_0

#define KEY_RIGHT_PORT GPIOD
#define KEY_RIGHT_PIN GPIO_PIN_6

#define KEY_ENTER_PORT GPIOE
#define KEY_ENTER_PIN GPIO_PIN_5

#define KEY_BACK_PORT GPIOE
#define KEY_BACK_PIN GPIO_PIN_1

#ifndef KEY_GPIO_PULL
#define KEY_GPIO_PULL GPIO_PULLUP
#endif

#ifndef KEY_PRESSED_LEVEL
#define KEY_PRESSED_LEVEL GPIO_PIN_RESET
#endif

#ifndef KEY_SCAN_TIM_INSTANCE
#define KEY_SCAN_TIM_INSTANCE TIM2
#endif

#ifndef KEY_SCAN_TIM_CLK_ENABLE
#define KEY_SCAN_TIM_CLK_ENABLE() __HAL_RCC_TIM2_CLK_ENABLE()
#endif

#ifndef KEY_SCAN_TIM_IRQn
#define KEY_SCAN_TIM_IRQn TIM2_IRQn
#endif

#ifndef KEY_SCAN_TIM_PRESCALER
#define KEY_SCAN_TIM_PRESCALER 8399U
#endif

#ifndef KEY_SCAN_TIM_PERIOD
#define KEY_SCAN_TIM_PERIOD 99U
#endif

#ifndef KEY_SCAN_TIM_IRQ_PREEMPT_PRIO
#define KEY_SCAN_TIM_IRQ_PREEMPT_PRIO 2U
#endif

#ifndef KEY_SCAN_TIM_IRQ_SUB_PRIO
#define KEY_SCAN_TIM_IRQ_SUB_PRIO 0U
#endif

typedef enum
{
    KEY_NONE = 0,
    KEY_UP,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_ENTER,
    KEY_BACK
} KeyEvent_t;

void Key_Init(void);
KeyEvent_t Key_Scan(void);
void Key_TimerCallback(TIM_HandleTypeDef *htim);
TIM_HandleTypeDef *Key_GetTimerHandle(void);

#endif
