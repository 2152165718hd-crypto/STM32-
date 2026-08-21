#include "./Hardware/LED/LED.h"

/**
 * @file LED.c
 * @brief 指示灯驱动实现。
 */

/**
 * @brief 初始化药盒指示灯 GPIO，并默认熄灭。
 */
void LED_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitStruct.Pin = MEDICINE_LED_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(MEDICINE_LED_PORT, &GPIO_InitStruct);

    HAL_GPIO_WritePin(MEDICINE_LED_PORT, MEDICINE_LED_PIN, GPIO_PIN_SET);
}

/**
 * @brief 点亮指定 LED。
 * @note 当前硬件为低电平有效。
 */
void LED_On(GPIO_TypeDef *LED_PORT, uint16_t led_pin)
{
    HAL_GPIO_WritePin(LED_PORT, led_pin, GPIO_PIN_RESET);
}

/**
 * @brief 熄灭指定 LED。
 */
void LED_Off(GPIO_TypeDef *LED_PORT, uint16_t led_pin)
{
    HAL_GPIO_WritePin(LED_PORT, led_pin, GPIO_PIN_SET);
}

/**
 * @brief 翻转指定 LED 状态。
 */
void LED_Toggle(GPIO_TypeDef *LED_PORT, uint16_t led_pin)
{
    HAL_GPIO_TogglePin(LED_PORT, led_pin);
}

/**
 * @brief 按布尔值设置指定 LED 状态。
 */
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
