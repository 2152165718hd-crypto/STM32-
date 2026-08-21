#include "HARDWARE/Independent_Key/KEY.h"

#define KEY_DEBOUNCE_CNT 2U
#define KEY_EVENT_BUF_SIZE 12U

typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pin;
    KeyEvent_t event;
    uint8_t stable;
    uint8_t last;
    uint8_t cnt;
} KeyState_t;

static KeyState_t s_keys[] = {
    {KEY_UP_PORT, KEY_UP_PIN, KEY_UP, 0U, 0U, 0U},
    {KEY_DOWN_PORT, KEY_DOWN_PIN, KEY_DOWN, 0U, 0U, 0U},
    {KEY_LEFT_PORT, KEY_LEFT_PIN, KEY_LEFT, 0U, 0U, 0U},
    {KEY_RIGHT_PORT, KEY_RIGHT_PIN, KEY_RIGHT, 0U, 0U, 0U},
    {KEY_ENTER_PORT, KEY_ENTER_PIN, KEY_ENTER, 0U, 0U, 0U},
    {KEY_BACK_PORT, KEY_BACK_PIN, KEY_BACK, 0U, 0U, 0U},
};

#define KEY_COUNT ((uint32_t)(sizeof(s_keys) / sizeof(s_keys[0])))

static volatile KeyEvent_t s_evBuf[KEY_EVENT_BUF_SIZE];
static volatile uint8_t s_evHead = 0U;
static volatile uint8_t s_evTail = 0U;

static TIM_HandleTypeDef s_keyTimer;

static void Key_EnablePortClock(GPIO_TypeDef *port)
{
    if (port == GPIOA)
    {
        __HAL_RCC_GPIOA_CLK_ENABLE();
    }
    else if (port == GPIOB)
    {
        __HAL_RCC_GPIOB_CLK_ENABLE();
    }
    else if (port == GPIOC)
    {
        __HAL_RCC_GPIOC_CLK_ENABLE();
    }
    else if (port == GPIOD)
    {
        __HAL_RCC_GPIOD_CLK_ENABLE();
    }
    else if (port == GPIOE)
    {
        __HAL_RCC_GPIOE_CLK_ENABLE();
    }
}

static uint8_t Key_ReadRaw(GPIO_TypeDef *port, uint16_t pin)
{
    return (HAL_GPIO_ReadPin(port, pin) == KEY_PRESSED_LEVEL) ? 1U : 0U;
}

static void Key_PushEvent(KeyEvent_t ev)
{
    uint8_t next = (uint8_t)((s_evTail + 1U) % KEY_EVENT_BUF_SIZE);

    if (next == s_evHead)
    {
        s_evHead = (uint8_t)((s_evHead + 1U) % KEY_EVENT_BUF_SIZE);
    }

    s_evBuf[s_evTail] = ev;
    s_evTail = next;
}

void Key_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    uint32_t i;

    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = KEY_GPIO_PULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

    for (i = 0U; i < KEY_COUNT; i++)
    {
        Key_EnablePortClock(s_keys[i].port);
        GPIO_InitStruct.Pin = s_keys[i].pin;
        HAL_GPIO_Init(s_keys[i].port, &GPIO_InitStruct);
    }

    for (i = 0U; i < KEY_COUNT; i++)
    {
        uint8_t raw = Key_ReadRaw(s_keys[i].port, s_keys[i].pin);
        s_keys[i].last = raw;
        s_keys[i].stable = raw;
        s_keys[i].cnt = KEY_DEBOUNCE_CNT;
    }

    KEY_SCAN_TIM_CLK_ENABLE();
    s_keyTimer.Instance = KEY_SCAN_TIM_INSTANCE;
    s_keyTimer.Init.Prescaler = KEY_SCAN_TIM_PRESCALER;
    s_keyTimer.Init.CounterMode = TIM_COUNTERMODE_UP;
    s_keyTimer.Init.Period = KEY_SCAN_TIM_PERIOD;
    s_keyTimer.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    s_keyTimer.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    (void)HAL_TIM_Base_Init(&s_keyTimer);

    HAL_NVIC_SetPriority(KEY_SCAN_TIM_IRQn, KEY_SCAN_TIM_IRQ_PREEMPT_PRIO, KEY_SCAN_TIM_IRQ_SUB_PRIO);
    HAL_NVIC_EnableIRQ(KEY_SCAN_TIM_IRQn);
    (void)HAL_TIM_Base_Start_IT(&s_keyTimer);
}

KeyEvent_t Key_Scan(void)
{
    KeyEvent_t ev;

    if (s_evHead == s_evTail)
    {
        return KEY_NONE;
    }

    __disable_irq();
    if (s_evHead == s_evTail)
    {
        __enable_irq();
        return KEY_NONE;
    }
    ev = s_evBuf[s_evHead];
    s_evHead = (uint8_t)((s_evHead + 1U) % KEY_EVENT_BUF_SIZE);
    __enable_irq();

    return ev;
}

void Key_TimerCallback(TIM_HandleTypeDef *htim)
{
    uint32_t i;

    if ((htim == NULL) || (htim->Instance != KEY_SCAN_TIM_INSTANCE))
    {
        return;
    }

    for (i = 0U; i < KEY_COUNT; i++)
    {
        uint8_t raw = Key_ReadRaw(s_keys[i].port, s_keys[i].pin);

        if (raw != s_keys[i].last)
        {
            s_keys[i].last = raw;
            s_keys[i].cnt = 0U;
        }
        else if (s_keys[i].cnt < KEY_DEBOUNCE_CNT)
        {
            s_keys[i].cnt++;
            if ((s_keys[i].cnt == KEY_DEBOUNCE_CNT) && (raw != s_keys[i].stable))
            {
                s_keys[i].stable = raw;
                if (raw != 0U)
                {
                    Key_PushEvent(s_keys[i].event);
                }
            }
        }
    }
}

TIM_HandleTypeDef *Key_GetTimerHandle(void)
{
    return &s_keyTimer;
}
