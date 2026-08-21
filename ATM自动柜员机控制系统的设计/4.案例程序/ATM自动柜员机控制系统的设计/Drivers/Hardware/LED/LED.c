#include ".\Hardware\LED\LED.h"

static uint8_t s_led_state[2] = {0u, 0u};

static void LED_Write(LED_Id_t led, GPIO_PinState state)
{
    if (led == LED_GATE)
    {
        HAL_GPIO_WritePin(LED1_GPIO_PORT, LED1_GPIO_PIN, state);
    }
    else
    {
        HAL_GPIO_WritePin(LED2_GPIO_PORT, LED2_GPIO_PIN, state);
    }
}

void LED_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitStruct.Pin = LED1_GPIO_PIN | LED2_GPIO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    LED_AllOff();
}

void LED_Set(LED_Id_t led, uint8_t on)
{
    if ((uint32_t)led >= 2u)
    {
        return;
    }

    s_led_state[led] = (on != 0u) ? 1u : 0u;
    LED_Write(led, s_led_state[led] ? LED_ON_LEVEL : LED_OFF_LEVEL);
}

void LED_Toggle(LED_Id_t led)
{
    if ((uint32_t)led >= 2u)
    {
        return;
    }

    LED_Set(led, s_led_state[led] == 0u);
}

void LED_AllOff(void)
{
    LED_Set(LED_GATE, 0u);
    LED_Set(LED_ALARM, 0u);
}

uint8_t LED_GetState(LED_Id_t led)
{
    if ((uint32_t)led >= 2u)
    {
        return 0u;
    }

    return s_led_state[led];
}
