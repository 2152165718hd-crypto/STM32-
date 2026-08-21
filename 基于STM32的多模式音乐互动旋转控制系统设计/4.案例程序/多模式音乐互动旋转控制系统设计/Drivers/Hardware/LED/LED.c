#include "./Hardware/LED/LED.h"

void LED_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitStruct.Pin = LED_GPIO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_GPIO_PORT, &GPIO_InitStruct);

    LED_Off(LED_GPIO_PORT, LED_GPIO_PIN);
}

void LED_On(GPIO_TypeDef *LED_PORT, uint16_t led_pin)
{
    HAL_GPIO_WritePin(LED_PORT, led_pin, GPIO_PIN_RESET);
}

void LED_Off(GPIO_TypeDef *LED_PORT, uint16_t led_pin)
{
    HAL_GPIO_WritePin(LED_PORT, led_pin, GPIO_PIN_SET);
}

void LED_Toggle(GPIO_TypeDef *LED_PORT, uint16_t led_pin)
{
    HAL_GPIO_TogglePin(LED_PORT, led_pin);
}

void LED_Set(GPIO_TypeDef *LED_PORT, uint16_t led_pin, int8_t on)
{
    if (on != 0)
    {
        LED_On(LED_PORT, led_pin);
    }
    else
    {
        LED_Off(LED_PORT, led_pin);
    }
}
