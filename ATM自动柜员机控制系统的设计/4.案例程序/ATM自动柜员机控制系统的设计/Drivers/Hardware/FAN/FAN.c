#include ".\Hardware\FAN\FAN.h"

static uint8_t s_fan_on = 0u;

void FAN_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitStruct.Pin = FAN_GPIO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(FAN_GPIO_PORT, &GPIO_InitStruct);

    FAN_Set(0u);
}

void FAN_Set(uint8_t on)
{
    s_fan_on = (on != 0u) ? 1u : 0u;
    HAL_GPIO_WritePin(FAN_GPIO_PORT, FAN_GPIO_PIN,
                      s_fan_on ? FAN_ACTIVE_LEVEL : FAN_INACTIVE_LEVEL);
}

uint8_t FAN_GetState(void)
{
    return s_fan_on;
}
