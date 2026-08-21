#include ".\Hardware\SR602\SR602.h"

typedef struct
{
    uint8_t raw_last;
    uint8_t stable_level;
    uint8_t stable_cnt;
    uint8_t ready;
    uint8_t armed;
    uint8_t motion_latched;
    uint32_t power_on_tick;
    uint32_t last_sample_tick;
    uint32_t rise_tick;
    uint32_t lock_until_tick;
} SR602_FilterState_t;

static SR602_FilterState_t s_sr602 = {0};

static uint8_t SR602_ReadPinLevel(void)
{
    return (HAL_GPIO_ReadPin(SR602_GPIO_PORT, SR602_GPIO_PIN) == GPIO_PIN_SET) ? 1U : 0U;
}

void SR602_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    uint8_t raw = 0U;

    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitStruct.Pin = SR602_GPIO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(SR602_GPIO_PORT, &GPIO_InitStruct);

    raw = SR602_ReadPinLevel();

    s_sr602.raw_last = raw;
    s_sr602.stable_level = raw;
    s_sr602.stable_cnt = SR602_FILTER_STABLE_CNT;
    s_sr602.ready = 0U;
    s_sr602.armed = 0U;
    s_sr602.motion_latched = 0U;
    s_sr602.power_on_tick = HAL_GetTick();
    s_sr602.last_sample_tick = s_sr602.power_on_tick;
    s_sr602.rise_tick = 0U;
    s_sr602.lock_until_tick = 0U;
}

void SR602_Update(void)
{
    uint32_t now = HAL_GetTick();
    uint8_t raw = 0U;
    uint8_t stable_changed = 0U;

    if ((now - s_sr602.last_sample_tick) < SR602_FILTER_SAMPLE_MS)
    {
        return;
    }
    s_sr602.last_sample_tick = now;

    raw = SR602_ReadPinLevel();

    if (raw != s_sr602.raw_last)
    {
        s_sr602.raw_last = raw;
        s_sr602.stable_cnt = 0U;
    }
    else if (s_sr602.stable_cnt < SR602_FILTER_STABLE_CNT)
    {
        s_sr602.stable_cnt++;
        if ((s_sr602.stable_cnt >= SR602_FILTER_STABLE_CNT) && (s_sr602.stable_level != raw))
        {
            s_sr602.stable_level = raw;
            stable_changed = 1U;
        }
    }

    if (s_sr602.ready == 0U)
    {
        if ((now - s_sr602.power_on_tick) < SR602_STARTUP_IGNORE_MS)
        {
            return;
        }

        s_sr602.ready = 1U;
        s_sr602.motion_latched = 0U;
        s_sr602.rise_tick = 0U;
        s_sr602.armed = (s_sr602.stable_level == 0U) ? 1U : 0U;
        return;
    }

    /* Startup 结束后必须先观测到低电平，再允许触发，避免把启动高电平误判为人体触发。 */
    if (s_sr602.armed == 0U)
    {
        if (s_sr602.stable_level == 0U)
        {
            s_sr602.armed = 1U;
        }
        return;
    }

    if (stable_changed != 0U)
    {
        if (s_sr602.stable_level != 0U)
        {
            s_sr602.rise_tick = now;
        }
        else
        {
            s_sr602.rise_tick = 0U;
        }
    }

    if ((int32_t)(now - s_sr602.lock_until_tick) < 0)
    {
        return;
    }

    if ((s_sr602.stable_level != 0U) && (s_sr602.rise_tick != 0U))
    {
        if ((now - s_sr602.rise_tick) >= SR602_VALID_HIGH_MS)
        {
            s_sr602.motion_latched = 1U;
            s_sr602.lock_until_tick = now + SR602_BLOCK_TIME_MS;
            s_sr602.rise_tick = 0U;
        }
    }
}

uint8_t SR602_IsReady(void)
{
    return s_sr602.ready;
}

uint8_t SR602_GetRawLevel(void)
{
    return SR602_ReadPinLevel();
}

uint8_t SR602_GetLevel(void)
{
    return (s_sr602.ready != 0U) ? s_sr602.stable_level : 0U;
}

uint8_t SR602_GetMotionEvent(void)
{
    uint8_t event = s_sr602.motion_latched;
    s_sr602.motion_latched = 0U;
    return event;
}
