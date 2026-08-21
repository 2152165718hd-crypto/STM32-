#include ".\Hardware\LED\LED.h"

static void LED_EnablePortClock(GPIO_TypeDef *port)
{
    if (port == GPIOA)
    {
        __HAL_RCC_GPIOA_CLK_ENABLE();
    }
    else if (port == GPIOB)
    {
        __HAL_RCC_GPIOB_CLK_ENABLE();
    }
    else if (port == GPIOC)
    {
        __HAL_RCC_GPIOC_CLK_ENABLE();
    }
    else if (port == GPIOD)
    {
        __HAL_RCC_GPIOD_CLK_ENABLE();
    }
#ifdef GPIOE
    else if (port == GPIOE)
    {
        __HAL_RCC_GPIOE_CLK_ENABLE();
    }
#endif
}

void LED_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    LED_EnablePortClock(LED_GPIO_PORT);

    GPIO_InitStruct.Pin = LED_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_GPIO_PORT, &GPIO_InitStruct);

    LED_Off();
}

void LED_On(void)
{
    HAL_GPIO_WritePin(LED_GPIO_PORT, LED_PIN, LED_ON_LEVEL);
}

void LED_Off(void)
{
    HAL_GPIO_WritePin(LED_GPIO_PORT, LED_PIN, LED_OFF_LEVEL);
}

void LED_Toggle(void)
{
    HAL_GPIO_TogglePin(LED_GPIO_PORT, LED_PIN);
}

uint8_t LED_IsOn(void)
{
    return (HAL_GPIO_ReadPin(LED_GPIO_PORT, LED_PIN) == LED_ON_LEVEL) ? 1u : 0u;
}
