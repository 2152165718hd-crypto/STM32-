#include ".\Hardware\KEY\KEY.h"

#define KEY_DEBOUNCE_COUNT 2u
#define KEY_EVENT_BUF_SIZE 8u
#define KEY_COUNT 4u

typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pin;
    uint8_t stable_level;
    uint8_t last_level;
    uint8_t debounce_count;
    uint8_t idle_level;
} KeyState_t;

static KeyState_t s_keys[KEY_COUNT] =
{
    {KEY_UP_PORT, KEY_UP_PIN, 1u, 1u, 0u, 1u},
    {KEY_DOWN_PORT, KEY_DOWN_PIN, 1u, 1u, 0u, 1u},
    {KEY_ENTER_PORT, KEY_ENTER_PIN, 1u, 1u, 0u, 1u},
    {KEY_BACK_PORT, KEY_BACK_PIN, 1u, 1u, 0u, 1u},
};

static const KeyEvent_t s_key_events[KEY_COUNT] =
{
    KEY_UP,
    KEY_DOWN,
    KEY_ENTER,
    KEY_BACK
};

static const uint8_t s_key_masks[KEY_COUNT] =
{
    KEY_MASK_UP,
    KEY_MASK_DOWN,
    KEY_MASK_ENTER,
    KEY_MASK_BACK
};

static volatile KeyEvent_t s_event_buffer[KEY_EVENT_BUF_SIZE];
static volatile uint8_t s_event_head = 0u;
static volatile uint8_t s_event_tail = 0u;

static TIM_HandleTypeDef s_key_timer;

static void Key_PushEvent(KeyEvent_t event)
{
    uint8_t next_tail = (uint8_t)((s_event_tail + 1u) % KEY_EVENT_BUF_SIZE);

    if (next_tail == s_event_head)
    {
        s_event_head = (uint8_t)((s_event_head + 1u) % KEY_EVENT_BUF_SIZE);
    }

    s_event_buffer[s_event_tail] = event;
    s_event_tail = next_tail;
}

static uint8_t Key_ReadRawLevel(const KeyState_t *key)
{
    return (HAL_GPIO_ReadPin(key->port, key->pin) == GPIO_PIN_SET) ? 1u : 0u;
}

void Key_Init(void)
{
    uint8_t index;
    GPIO_InitTypeDef gpio_init = {0};

    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_AFIO_REMAP_SWJ_NOJTAG();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_TIM2_CLK_ENABLE();

    gpio_init.Mode = GPIO_MODE_INPUT;
    gpio_init.Pull = GPIO_PULLUP;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;

    gpio_init.Pin = KEY_UP_PIN;
    HAL_GPIO_Init(KEY_UP_PORT, &gpio_init);

    gpio_init.Pin = KEY_DOWN_PIN;
    HAL_GPIO_Init(KEY_DOWN_PORT, &gpio_init);

    gpio_init.Pin = KEY_ENTER_PIN;
    HAL_GPIO_Init(KEY_ENTER_PORT, &gpio_init);

    gpio_init.Pin = KEY_BACK_PIN;
    HAL_GPIO_Init(KEY_BACK_PORT, &gpio_init);

    s_event_head = 0u;
    s_event_tail = 0u;

    for (index = 0u; index < KEY_COUNT; index++)
    {
        uint8_t raw_level = Key_ReadRawLevel(&s_keys[index]);
        s_keys[index].idle_level = raw_level;
        s_keys[index].stable_level = raw_level;
        s_keys[index].last_level = raw_level;
        s_keys[index].debounce_count = KEY_DEBOUNCE_COUNT;
    }

    s_key_timer.Instance = TIM2;
    s_key_timer.Init.Prescaler = 7199u;
    s_key_timer.Init.CounterMode = TIM_COUNTERMODE_UP;
    s_key_timer.Init.Period = 99u;
    s_key_timer.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    s_key_timer.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_TIM_Base_Init(&s_key_timer);

    HAL_NVIC_SetPriority(TIM2_IRQn, 1u, 0u);
    HAL_NVIC_EnableIRQ(TIM2_IRQn);
    HAL_TIM_Base_Start_IT(&s_key_timer);
}

KeyEvent_t Key_Scan(void)
{
    KeyEvent_t event;

    if (s_event_head == s_event_tail)
    {
        return KEY_NONE;
    }

    event = s_event_buffer[s_event_head];
    s_event_head = (uint8_t)((s_event_head + 1u) % KEY_EVENT_BUF_SIZE);
    return event;
}

uint8_t Key_GetStableMask(void)
{
    uint8_t mask = 0u;
    uint8_t index;

    for (index = 0u; index < KEY_COUNT; index++)
    {
        if (s_keys[index].stable_level != s_keys[index].idle_level)
        {
            mask |= s_key_masks[index];
        }
    }

    return mask;
}

void Key_ClearEvents(void)
{
    s_event_head = s_event_tail;
}

void Key_TimerCallback(TIM_HandleTypeDef *htim)
{
    uint8_t index;

    if ((htim == NULL) || (htim->Instance != TIM2))
    {
        return;
    }

    for (index = 0u; index < KEY_COUNT; index++)
    {
        uint8_t raw_level = Key_ReadRawLevel(&s_keys[index]);

        if (raw_level != s_keys[index].last_level)
        {
            s_keys[index].last_level = raw_level;
            s_keys[index].debounce_count = 0u;
            continue;
        }

        if (s_keys[index].debounce_count < KEY_DEBOUNCE_COUNT)
        {
            s_keys[index].debounce_count++;
        }

        if ((s_keys[index].debounce_count == KEY_DEBOUNCE_COUNT) &&
            (raw_level != s_keys[index].stable_level))
        {
            s_keys[index].stable_level = raw_level;
            if (raw_level != s_keys[index].idle_level)
            {
                Key_PushEvent(s_key_events[index]);
            }
        }
    }
}

void TIM2_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&s_key_timer);
}
