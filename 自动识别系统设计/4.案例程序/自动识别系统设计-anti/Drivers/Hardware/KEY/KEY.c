#include ".\Hardware\KEY\KEY.h"
#include <stdint.h>

/* 消抖计数阈值：KEY_DEBOUNCE_CNT × 10ms = 20ms */
#define KEY_DEBOUNCE_CNT 2u

/* 事件循环队列容量 */
#define KEY_EVENT_BUF_SIZE 8u

typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pin;
    uint8_t stable; /* 稳定状态：0=未按, 1=已按 */
    uint8_t last;   /* 上一次原始电平值 */
    uint8_t cnt;    /* 原始值连续稳定计数 */
} KeyState_t;

static KeyState_t s_keys[4] = {
    {KEY_UP_PORT, KEY_UP_PIN, 0u, 0u, 0u},
    {KEY_DOWN_PORT, KEY_DOWN_PIN, 0u, 0u, 0u},
    {KEY_ENTER_PORT, KEY_ENTER_PIN, 0u, 0u, 0u},
    {KEY_BACK_PORT, KEY_BACK_PIN, 0u, 0u, 0u},
};

/* 事件循环队列 */
static volatile KeyEvent_t s_evBuf[KEY_EVENT_BUF_SIZE];
static volatile uint8_t s_evHead = 0u;
static volatile uint8_t s_evTail = 0u;

TIM_HandleTypeDef htim2_key;

static void Key_PushEvent(KeyEvent_t ev)
{
    uint8_t next = (s_evTail + 1u) % KEY_EVENT_BUF_SIZE;
    if (next == s_evHead)
    {
        /* 队满，丢弃最旧事件 */
        s_evHead = (s_evHead + 1u) % KEY_EVENT_BUF_SIZE;
    }
    s_evBuf[s_evTail] = ev;
    s_evTail = next;
}

void Key_Init(void)
{
    /* GPIO 初始化*/
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP; /* 上拉，按下为低电平 */
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

    GPIO_InitStruct.Pin = KEY_UP_PIN;
    HAL_GPIO_Init(KEY_UP_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = KEY_DOWN_PIN;
    HAL_GPIO_Init(KEY_DOWN_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = KEY_ENTER_PIN;
    HAL_GPIO_Init(KEY_ENTER_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = KEY_BACK_PIN;
    HAL_GPIO_Init(KEY_BACK_PORT, &GPIO_InitStruct);

    /* 读取上电初始电平，避免首次扫描误判 */
    for (int i = 0; i < 4; i++)
    {
        uint8_t raw = (HAL_GPIO_ReadPin(s_keys[i].port, s_keys[i].pin) == GPIO_PIN_RESET) ? 1u : 0u;
        s_keys[i].last = raw;
        s_keys[i].stable = raw;
        s_keys[i].cnt = KEY_DEBOUNCE_CNT; /* 视为已稳定 */
    }

    /* TIM2 初始化：10ms 定时中断
     * 系统时钟 72MHz，PSC=7199 → 计数时钟 10kHz，ARR=99 → 周期 10ms */
    __HAL_RCC_TIM2_CLK_ENABLE();
    htim2_key.Instance = TIM2;
    htim2_key.Init.Prescaler = 7199u;
    htim2_key.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim2_key.Init.Period = 99u;
    htim2_key.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim2_key.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_TIM_Base_Init(&htim2_key);

    HAL_NVIC_SetPriority(TIM2_IRQn, 1u, 0u);
    HAL_NVIC_EnableIRQ(TIM2_IRQn);
    HAL_TIM_Base_Start_IT(&htim2_key);
}

/**
 * @brief  从事件队列取出一个按键事件（供主循环调用）
 * @retval 按键事件，队列为空时返回 KEY_NONE
 */
KeyEvent_t Key_Scan(void)
{
    if (s_evHead == s_evTail)
    {
        return KEY_NONE;
    }
    KeyEvent_t ev = s_evBuf[s_evHead];
    s_evHead = (s_evHead + 1u) % KEY_EVENT_BUF_SIZE;
    return ev;
}

/**
 * @brief  按键定时器回调，每 10ms 执行一次消抖扫描
 * @note   需要在用户的 HAL_TIM_PeriodElapsedCallback 中调用此函数
 * @param  htim: 定时器句柄指针
 */
void Key_TimerCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance != TIM2)
    {
        return;
    }

    static const KeyEvent_t evMap[4] = {KEY_UP, KEY_DOWN, KEY_ENTER, KEY_BACK};

    for (int i = 0; i < 4; i++)
    {
        uint8_t raw = (HAL_GPIO_ReadPin(s_keys[i].port, s_keys[i].pin) == GPIO_PIN_RESET) ? 1u : 0u;

        if (raw != s_keys[i].last)
        {
            /* 电平发生变化，重置稳定计数 */
            s_keys[i].last = raw;
            s_keys[i].cnt = 0u;
        }
        else if (s_keys[i].cnt < KEY_DEBOUNCE_CNT)
        {
            s_keys[i].cnt++;
            if (s_keys[i].cnt == KEY_DEBOUNCE_CNT)
            {
                /* 电平连续稳定已达消抖阈值 */
                if (raw != s_keys[i].stable)
                {
                    s_keys[i].stable = raw;
                    if (raw == 1u)
                    {
                        /* 上升沿（未按 → 按下），写入事件队列 */
                        Key_PushEvent(evMap[i]);
                    }
                }
            }
        }
    }
}

/**
 * @brief  TIM2 中断处理函数（按键定时扫描）
 */
void TIM2_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&htim2_key);
}
