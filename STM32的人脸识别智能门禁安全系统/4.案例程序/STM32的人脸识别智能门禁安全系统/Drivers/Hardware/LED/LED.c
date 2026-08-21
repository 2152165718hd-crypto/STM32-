#include "./Hardware/LED/LED.h"

void LED_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* Configure GPIO pins for LEDs on GPIOB */
    GPIO_InitStruct.Pin = FILLLIGHT_LED_PIN | RED_LED_PIN | GREEN_LED_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* Turn off all LEDs by default (Active low, so set HIGH) */
    HAL_GPIO_WritePin(FILLLIGHT_LED_GPIO_PORT, FILLLIGHT_LED_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(RED_LED_GPIO_PORT, RED_LED_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GREEN_LED_GPIO_PORT, GREEN_LED_PIN, GPIO_PIN_SET);
}

void LED_On(GPIO_TypeDef *LED_PORT, uint16_t led_pin)
{
    HAL_GPIO_WritePin(LED_PORT, led_pin, GPIO_PIN_RESET); // Active low
}

void LED_Off(GPIO_TypeDef *LED_PORT, uint16_t led_pin)
{
    HAL_GPIO_WritePin(LED_PORT, led_pin, GPIO_PIN_SET); // Active low
}

void LED_Toggle(GPIO_TypeDef *LED_PORT, uint16_t led_pin)
{
    HAL_GPIO_TogglePin(LED_PORT, led_pin);
}

void LED_Set(GPIO_TypeDef *LED_PORT, uint16_t led_pin, int8_t on)
{
    if (on)
    {
        LED_On(LED_PORT, led_pin);
    }
    else
    {
        LED_Off(LED_PORT, led_pin);
    }
}
