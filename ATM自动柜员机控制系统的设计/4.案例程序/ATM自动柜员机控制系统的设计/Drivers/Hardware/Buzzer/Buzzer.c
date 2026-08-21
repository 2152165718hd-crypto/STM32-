#include ".\Hardware\Buzzer\Buzzer.h"

static uint8_t s_buzzer_on = 0u;

static GPIO_PinState Buzzer_InactiveLevel(void)
{
    return (BUZZER_ACTIVE_LEVEL == GPIO_PIN_SET) ? GPIO_PIN_RESET : GPIO_PIN_SET;
}

void Buzzer_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitStruct.Pin = BUZZER_GPIO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BUZZER_GPIO_PORT, &GPIO_InitStruct);

    Buzzer_Off();
}

void Buzzer_Set(uint8_t on)
{
    s_buzzer_on = (on != 0u) ? 1u : 0u;
    HAL_GPIO_WritePin(BUZZER_GPIO_PORT, BUZZER_GPIO_PIN,
                      s_buzzer_on ? BUZZER_ACTIVE_LEVEL : Buzzer_InactiveLevel());
}

void Buzzer_On(void)
{
    Buzzer_Set(1u);
}

void Buzzer_Off(void)
{
    Buzzer_Set(0u);
}

uint8_t Buzzer_GetState(void)
{
    return s_buzzer_on;
}
