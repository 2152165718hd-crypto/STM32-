#include ".\Hardware\KEY\KEY.h"
#include <stdint.h>

#define KEY_DEBOUNCE_CNT 2u
#define KEY_EVENT_BUF_SIZE 8u
#define KEY_COUNT 4u

typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pin;
    uint8_t stable;
    uint8_t last;
    uint8_t cnt;
} KeyState_t;

static KeyState_t s_keys[KEY_COUNT] = {
    {KEY_UP_PORT, KEY_UP_PIN, 0u, 0u, 0u},
    {KEY_DOWN_PORT, KEY_DOWN_PIN, 0u, 0u, 0u},
    {KEY_ENTER_PORT, KEY_ENTER_PIN, 0u, 0u, 0u},
    {KEY_BACK_PORT, KEY_BACK_PIN, 0u, 0u, 0u},
};

static volatile KeyEvent_t s_evBuf[KEY_EVENT_BUF_SIZE];
static volatile uint8_t s_evHead = 0u;
static volatile uint8_t s_evTail = 0u;

static TIM_HandleTypeDef htim4_key;

static void Key_EnableGpioClock(GPIO_TypeDef *port)
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
#if defined(GPIOD) && defined(__HAL_RCC_GPIOD_CLK_ENABLE)
    else if (port == GPIOD)
    {
        __HAL_RCC_GPIOD_CLK_ENABLE();
    }
#endif
#if defined(GPIOE) && defined(__HAL_RCC_GPIOE_CLK_ENABLE)
    else if (port == GPIOE)
    {
        __HAL_RCC_GPIOE_CLK_ENABLE();
    }
#endif
}

static void Key_PushEvent(KeyEvent_t ev)
{
    uint8_t next = (uint8_t)((s_evTail + 1u) % KEY_EVENT_BUF_SIZE);

    if (next == s_evHead)
    {
        s_evHead = (uint8_t)((s_evHead + 1u) % KEY_EVENT_BUF_SIZE);
    }

    s_evBuf[s_evTail] = ev;
    s_evTail = next;
}

void Key_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    uint8_t i;

    for (i = 0u; i < KEY_COUNT; i++)
    {
        Key_EnableGpioClock(s_keys[i].port);
    }

    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

    for (i = 0u; i < KEY_COUNT; i++)
    {
        GPIO_InitStruct.Pin = s_keys[i].pin;
        HAL_GPIO_Init(s_keys[i].port, &GPIO_InitStruct);
    }

    for (i = 0u; i < KEY_COUNT; i++)
    {
        uint8_t raw = (HAL_GPIO_ReadPin(s_keys[i].port, s_keys[i].pin) == GPIO_PIN_RESET) ? 1u : 0u;
        s_keys[i].last = raw;
        s_keys[i].stable = raw;
        s_keys[i].cnt = KEY_DEBOUNCE_CNT;
    }

    __HAL_RCC_TIM4_CLK_ENABLE();
    htim4_key.Instance = TIM4;
    htim4_key.Init.Prescaler = 7199u;
    htim4_key.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim4_key.Init.Period = 99u;
    htim4_key.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim4_key.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_TIM_Base_Init(&htim4_key);

    HAL_NVIC_SetPriority(TIM4_IRQn, 1u, 0u);
    HAL_NVIC_EnableIRQ(TIM4_IRQn);
    HAL_TIM_Base_Start_IT(&htim4_key);
}

KeyEvent_t Key_Scan(void)
{
    KeyEvent_t ev;

    if (s_evHead == s_evTail)
    {
        return KEY_NONE;
    }

    ev = s_evBuf[s_evHead];
    s_evHead = (uint8_t)((s_evHead + 1u) % KEY_EVENT_BUF_SIZE);
    return ev;
}

void Key_TimerCallback(TIM_HandleTypeDef *htim)
{
    uint8_t i;
    static const KeyEvent_t evMap[KEY_COUNT] = {KEY_UP, KEY_DOWN, KEY_ENTER, KEY_BACK};

    if (htim->Instance != TIM4)
    {
        return;
    }

    for (i = 0u; i < KEY_COUNT; i++)
    {
        uint8_t raw = (HAL_GPIO_ReadPin(s_keys[i].port, s_keys[i].pin) == GPIO_PIN_RESET) ? 1u : 0u;

        if (raw != s_keys[i].last)
        {
            s_keys[i].last = raw;
            s_keys[i].cnt = 0u;
        }
        else if (s_keys[i].cnt < KEY_DEBOUNCE_CNT)
        {
            s_keys[i].cnt++;
            if ((s_keys[i].cnt == KEY_DEBOUNCE_CNT) && (raw != s_keys[i].stable))
            {
                s_keys[i].stable = raw;
                if (raw == 1u)
                {
                    Key_PushEvent(evMap[i]);
                }
            }
        }
    }
}

void TIM4_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&htim4_key);
}
