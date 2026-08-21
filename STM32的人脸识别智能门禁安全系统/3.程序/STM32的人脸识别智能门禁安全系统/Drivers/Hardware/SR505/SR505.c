#include ".\Hardware\SR505\SR505.h"

/* ====================================================================
 * STM32 人脸识别智能门禁系统 - SR505 红外人体传感器驱动
 * ==================================================================== */

typedef enum
{
    SR505_STATE_IDLE = 0,
    SR505_STATE_RISE_CONFIRM,
    SR505_STATE_ACTIVE,
    SR505_STATE_FALL_CONFIRM
} SR505_State_t;

static SR505_State_t g_sr505_state = SR505_STATE_IDLE;
static uint32_t g_sr505_boot_tick = 0;
static uint32_t g_sr505_last_sample_tick = 0;
static uint32_t g_sr505_state_tick = 0;
static uint32_t g_sr505_release_tick = 0;

void SR505_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = SR505_GPIO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(SR505_GPIO_PORT, &GPIO_InitStruct);

    g_sr505_state = SR505_STATE_IDLE;
    g_sr505_boot_tick = HAL_GetTick();
    g_sr505_last_sample_tick = g_sr505_boot_tick;
    g_sr505_state_tick = g_sr505_boot_tick;
    g_sr505_release_tick = g_sr505_boot_tick;
}

uint8_t SR505_ReadFiltered(void)
{
    uint32_t now = HAL_GetTick();
    uint8_t raw = (HAL_GPIO_ReadPin(SR505_GPIO_PORT, SR505_GPIO_PIN) == GPIO_PIN_SET) ? 1U : 0U;

    /* 上电预热期间，固定返回无人状态 */
    if ((now - g_sr505_boot_tick) < SR505_WARMUP_MS)
    {
        g_sr505_state = SR505_STATE_IDLE;
        g_sr505_state_tick = now;
        return 0U;
    }

    /* 未到采样周期时，返回当前滤波后的稳定状态 */
    if ((now - g_sr505_last_sample_tick) < SR505_SAMPLE_INTERVAL_MS)
    {
        return (g_sr505_state == SR505_STATE_ACTIVE || g_sr505_state == SR505_STATE_FALL_CONFIRM) ? 1U : 0U;
    }
    g_sr505_last_sample_tick = now;

    switch (g_sr505_state)
    {
    case SR505_STATE_IDLE:
        if (raw && (now - g_sr505_release_tick) >= SR505_RETRIGGER_BLOCK_MS)
        {
            g_sr505_state = SR505_STATE_RISE_CONFIRM;
            g_sr505_state_tick = now;
        }
        break;

    case SR505_STATE_RISE_CONFIRM:
        if (!raw)
        {
            g_sr505_state = SR505_STATE_IDLE;
            g_sr505_state_tick = now;
        }
        else if ((now - g_sr505_state_tick) >= SR505_RISE_CONFIRM_MS)
        {
            g_sr505_state = SR505_STATE_ACTIVE;
            g_sr505_state_tick = now;
        }
        break;

    case SR505_STATE_ACTIVE:
        if (!raw)
        {
            g_sr505_state = SR505_STATE_FALL_CONFIRM;
            g_sr505_state_tick = now;
        }
        break;

    case SR505_STATE_FALL_CONFIRM:
        if (raw)
        {
            g_sr505_state = SR505_STATE_ACTIVE;
            g_sr505_state_tick = now;
        }
        else if ((now - g_sr505_state_tick) >= SR505_FALL_CONFIRM_MS)
        {
            g_sr505_state = SR505_STATE_IDLE;
            g_sr505_state_tick = now;
            g_sr505_release_tick = now;
        }
        break;

    default:
        g_sr505_state = SR505_STATE_IDLE;
        g_sr505_state_tick = now;
        g_sr505_release_tick = now;
        break;
    }

    return (g_sr505_state == SR505_STATE_ACTIVE || g_sr505_state == SR505_STATE_FALL_CONFIRM) ? 1U : 0U;
}

uint8_t SR505_IsWarmupMasking(void)
{
    return ((HAL_GetTick() - g_sr505_boot_tick) < SR505_WARMUP_MS) ? 1U : 0U;
}
