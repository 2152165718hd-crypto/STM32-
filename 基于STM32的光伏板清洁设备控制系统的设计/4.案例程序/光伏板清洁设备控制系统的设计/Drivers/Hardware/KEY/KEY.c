#include "Hardware/KEY/KEY.h"

#define KEY_DEBOUNCE_CNT 2u
#define KEY_EVENT_BUF_SIZE 8u

typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pin;
    KeyEvent_t event;
    uint8_t stable;
    uint8_t last;
    uint8_t cnt;
    uint16_t hold_ticks;
    uint16_t repeat_ticks;
} KeyState_t;

static KeyState_t g_keys[4] = {
    {KEY_UP_PORT, KEY_UP_PIN, KEY_UP, 0u, 0u, 0u},
    {KEY_DOWN_PORT, KEY_DOWN_PIN, KEY_DOWN, 0u, 0u, 0u},
    {KEY_ENTER_PORT, KEY_ENTER_PIN, KEY_ENTER, 0u, 0u, 0u},
    {KEY_BACK_PORT, KEY_BACK_PIN, KEY_BACK, 0u, 0u, 0u},
};

#define KEY_COUNT (sizeof(g_keys) / sizeof(g_keys[0]))

static volatile KeyEvent_t g_event_buffer[KEY_EVENT_BUF_SIZE];
static volatile uint8_t g_event_head = 0u;
static volatile uint8_t g_event_tail = 0u;
static TIM_HandleTypeDef g_key_timer;

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
}

static void Key_TryReleaseSwjPins(void)
{
#if (KEY_AUTO_DISABLE_JTAG_FOR_SWJ_PINS != 0u)
    uint32_t i;
    uint8_t need_remap = 0u;

    for (i = 0u; i < KEY_COUNT; i++)
    {
        if ((g_keys[i].port == GPIOA && g_keys[i].pin == GPIO_PIN_15) ||
            (g_keys[i].port == GPIOB && (g_keys[i].pin == GPIO_PIN_3 || g_keys[i].pin == GPIO_PIN_4)))
        {
            need_remap = 1u;
            break;
        }
    }

    if (need_remap != 0u)
    {
        __HAL_RCC_AFIO_CLK_ENABLE();
        __HAL_AFIO_REMAP_SWJ_NOJTAG();
    }
#endif
}

static uint8_t Key_ReadRaw(GPIO_TypeDef *port, uint16_t pin)
{
    return (HAL_GPIO_ReadPin(port, pin) == KEY_PRESSED_LEVEL) ? 1u : 0u;
}

static uint8_t Key_IsRepeatEnabled(KeyEvent_t event)
{
    return (uint8_t)((event == KEY_UP) || (event == KEY_DOWN));
}

static void Key_PushEvent(KeyEvent_t event)
{
    uint8_t next = (uint8_t)((g_event_tail + 1u) % KEY_EVENT_BUF_SIZE);

    if (next == g_event_head)
    {
        g_event_head = (uint8_t)((g_event_head + 1u) % KEY_EVENT_BUF_SIZE);
    }

    g_event_buffer[g_event_tail] = event;
    g_event_tail = next;
}

void Key_Init(void)
{
    uint32_t i;
    GPIO_InitTypeDef gpio_init = {0};

    Key_TryReleaseSwjPins();

    gpio_init.Mode = GPIO_MODE_INPUT;
    gpio_init.Pull = KEY_GPIO_PULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;

    for (i = 0u; i < KEY_COUNT; i++)
    {
        Key_EnablePortClock(g_keys[i].port);
        gpio_init.Pin = g_keys[i].pin;
        HAL_GPIO_Init(g_keys[i].port, &gpio_init);
    }

    for (i = 0u; i < KEY_COUNT; i++)
    {
        uint8_t raw = Key_ReadRaw(g_keys[i].port, g_keys[i].pin);
        g_keys[i].last = raw;
        g_keys[i].stable = raw;
        g_keys[i].cnt = KEY_DEBOUNCE_CNT;
        g_keys[i].hold_ticks = 0u;
        g_keys[i].repeat_ticks = 0u;
    }

    KEY_SCAN_TIM_CLK_ENABLE();
    g_key_timer.Instance = KEY_SCAN_TIM_INSTANCE;
    g_key_timer.Init.Prescaler = KEY_SCAN_TIM_PRESCALER;
    g_key_timer.Init.CounterMode = TIM_COUNTERMODE_UP;
    g_key_timer.Init.Period = KEY_SCAN_TIM_PERIOD;
    g_key_timer.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    g_key_timer.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_TIM_Base_Init(&g_key_timer);

    HAL_NVIC_SetPriority(KEY_SCAN_TIM_IRQn, KEY_SCAN_TIM_IRQ_PREEMPT_PRIO, KEY_SCAN_TIM_IRQ_SUB_PRIO);
    HAL_NVIC_EnableIRQ(KEY_SCAN_TIM_IRQn);
    HAL_TIM_Base_Start_IT(&g_key_timer);
}

KeyEvent_t Key_Scan(void)
{
    KeyEvent_t event;

    if (g_event_head == g_event_tail)
    {
        return KEY_NONE;
    }

    event = g_event_buffer[g_event_head];
    g_event_head = (uint8_t)((g_event_head + 1u) % KEY_EVENT_BUF_SIZE);
    return event;
}

void Key_TimerCallback(TIM_HandleTypeDef *htim)
{
    uint32_t i;

    if (htim->Instance != KEY_SCAN_TIM_INSTANCE)
    {
        return;
    }

    for (i = 0u; i < KEY_COUNT; i++)
    {
        uint8_t raw = Key_ReadRaw(g_keys[i].port, g_keys[i].pin);

        if (raw != g_keys[i].last)
        {
            g_keys[i].last = raw;
            g_keys[i].cnt = 0u;
            g_keys[i].hold_ticks = 0u;
            g_keys[i].repeat_ticks = 0u;
        }
        else if (g_keys[i].cnt < KEY_DEBOUNCE_CNT)
        {
            g_keys[i].cnt++;

            if (g_keys[i].cnt == KEY_DEBOUNCE_CNT)
            {
                if (raw != g_keys[i].stable)
                {
                    g_keys[i].stable = raw;

                    if (raw == 1u)
                    {
                        Key_PushEvent(g_keys[i].event);
                    }
                    g_keys[i].hold_ticks = 0u;
                    g_keys[i].repeat_ticks = 0u;
                }
            }
        }
        else if ((g_keys[i].stable == 1u) && (Key_IsRepeatEnabled(g_keys[i].event) != 0u))
        {
            if (g_keys[i].hold_ticks < KEY_REPEAT_START_TICKS)
            {
                g_keys[i].hold_ticks++;
                g_keys[i].repeat_ticks = 0u;
            }
            else
            {
                g_keys[i].repeat_ticks++;
                if (g_keys[i].repeat_ticks >= KEY_REPEAT_INTERVAL_TICKS)
                {
                    g_keys[i].repeat_ticks = 0u;
                    Key_PushEvent(g_keys[i].event);
                }
            }
        }
    }
}

void KEY_SCAN_TIM_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&g_key_timer);
}
