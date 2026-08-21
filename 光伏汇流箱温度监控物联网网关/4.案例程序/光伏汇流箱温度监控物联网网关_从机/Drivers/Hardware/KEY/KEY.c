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
    KeyEvent_t event;
    uint8_t stable; /* 稳定状态：0=未按, 1=已按 */
    uint8_t last;   /* 上一次原始电平值 */
    uint8_t cnt;    /* 原始值连续稳定计数 */
} KeyState_t;

static KeyState_t s_keys[4] = {
    {KEY_UP_PORT, KEY_UP_PIN, KEY_UP, 0u, 0u, 0u},
    {KEY_DOWN_PORT, KEY_DOWN_PIN, KEY_DOWN, 0u, 0u, 0u},
    {KEY_ENTER_PORT, KEY_ENTER_PIN, KEY_ENTER, 0u, 0u, 0u},
    {KEY_BACK_PORT, KEY_BACK_PIN, KEY_BACK, 0u, 0u, 0u},
};

#define KEY_COUNT (sizeof(s_keys) / sizeof(s_keys[0]))

/* 事件循环队列 */
static volatile KeyEvent_t s_evBuf[KEY_EVENT_BUF_SIZE];
static volatile uint8_t s_evHead = 0u;
static volatile uint8_t s_evTail = 0u;

TIM_HandleTypeDef htim2_key;

static void Key_EnablePortClock(GPIO_TypeDef *port)
{
    if (port == GPIOA)
    {
        __HAL_RCC_GPIOA_CLK_ENABLE();
    }
#if defined(GPIOB)
    else if (port == GPIOB)
    {
        __HAL_RCC_GPIOB_CLK_ENABLE();
    }
#endif
#if defined(GPIOC)
    else if (port == GPIOC)
    {
        __HAL_RCC_GPIOC_CLK_ENABLE();
    }
#endif
#if defined(GPIOD)
    else if (port == GPIOD)
    {
        __HAL_RCC_GPIOD_CLK_ENABLE();
    }
#endif
#if defined(GPIOE)
    else if (port == GPIOE)
    {
        __HAL_RCC_GPIOE_CLK_ENABLE();
    }
#endif
#if defined(GPIOF)
    else if (port == GPIOF)
    {
        __HAL_RCC_GPIOF_CLK_ENABLE();
    }
#endif
#if defined(GPIOG)
    else if (port == GPIOG)
    {
        __HAL_RCC_GPIOG_CLK_ENABLE();
    }
#endif
    else
    {
        /* 未知端口，不做处理 */
    }
}

static uint8_t Key_ReadRaw(GPIO_TypeDef *port, uint16_t pin)
{
    return (HAL_GPIO_ReadPin(port, pin) == KEY_PRESSED_LEVEL) ? 1u : 0u;
}

static void Key_TryReleaseSwjPins(void)
{
#if (KEY_AUTO_DISABLE_JTAG_FOR_SWJ_PINS != 0u)
    uint8_t needRemap = 0u;

    for (uint32_t i = 0u; i < KEY_COUNT; i++)
    {
        if ((s_keys[i].port == GPIOA && s_keys[i].pin == GPIO_PIN_15) ||
            (s_keys[i].port == GPIOB && (s_keys[i].pin == GPIO_PIN_3 || s_keys[i].pin == GPIO_PIN_4)))
        {
            needRemap = 1u;
            break;
        }
    }

    if (needRemap != 0u)
    {
        __HAL_RCC_AFIO_CLK_ENABLE();
        __HAL_AFIO_REMAP_SWJ_NOJTAG();
    }
#endif
}

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
    Key_TryReleaseSwjPins();

    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = KEY_GPIO_PULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

    for (uint32_t i = 0u; i < KEY_COUNT; i++)
    {
        Key_EnablePortClock(s_keys[i].port);
        GPIO_InitStruct.Pin = s_keys[i].pin;
        HAL_GPIO_Init(s_keys[i].port, &GPIO_InitStruct);
    }

    /* 读取上电初始电平，避免首次扫描误判 */
    for (uint32_t i = 0u; i < KEY_COUNT; i++)
    {
        uint8_t raw = Key_ReadRaw(s_keys[i].port, s_keys[i].pin);
        s_keys[i].last = raw;
        s_keys[i].stable = raw;
        s_keys[i].cnt = KEY_DEBOUNCE_CNT; /* 视为已稳定 */
    }

    /* 定时器初始化：默认 10ms 周期（可在 KEY.h 中通过宏重配置） */
    KEY_SCAN_TIM_CLK_ENABLE();
    htim2_key.Instance = KEY_SCAN_TIM_INSTANCE;
    htim2_key.Init.Prescaler = KEY_SCAN_TIM_PRESCALER;
    htim2_key.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim2_key.Init.Period = KEY_SCAN_TIM_PERIOD;
    htim2_key.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim2_key.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_TIM_Base_Init(&htim2_key);

    HAL_NVIC_SetPriority(KEY_SCAN_TIM_IRQn, KEY_SCAN_TIM_IRQ_PREEMPT_PRIO, KEY_SCAN_TIM_IRQ_SUB_PRIO);
    HAL_NVIC_EnableIRQ(KEY_SCAN_TIM_IRQn);
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
    if (htim->Instance != KEY_SCAN_TIM_INSTANCE)
    {
        return;
    }

    for (uint32_t i = 0u; i < KEY_COUNT; i++)
    {
        uint8_t raw = Key_ReadRaw(s_keys[i].port, s_keys[i].pin);

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
                        Key_PushEvent(s_keys[i].event);
                    }
                }
            }
        }
    }
}

/**
 * @brief  按键扫描定时器中断处理函数（由宏 KEY_SCAN_TIM_IRQHandler 指定）
 */
void KEY_SCAN_TIM_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&htim2_key);
}
