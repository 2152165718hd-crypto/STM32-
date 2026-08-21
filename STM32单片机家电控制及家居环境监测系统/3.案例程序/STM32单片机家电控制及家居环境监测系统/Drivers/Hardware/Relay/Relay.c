#include ".\Hardware\Relay\Relay.h"

static GPIO_PinState Relay_GetPinState(uint8_t state)
{
    if (ACTIVE_LEVEL)
    {
        return state ? GPIO_PIN_SET : GPIO_PIN_RESET;
    }
    else
    {
        return state ? GPIO_PIN_RESET : GPIO_PIN_SET;
    }
}

void Relay_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitStruct.Pin = RELAY_AC_PIN | RELAY_RC_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(RELAY_GPIO_PORT, &GPIO_InitStruct);

    Relay_SetState(0, 0);
    Relay_SetState(1, 0);
}

void Relay_SetState(uint8_t relay_id, uint8_t state)
{
    uint16_t pin;

    if (relay_id == 0)
    {
        pin = RELAY_AC_PIN;
    }
    else if (relay_id == 1)
    {
        pin = RELAY_RC_PIN;
    }
    else
    {
        return;
    }

    HAL_GPIO_WritePin(RELAY_GPIO_PORT, pin, Relay_GetPinState(state));
}
