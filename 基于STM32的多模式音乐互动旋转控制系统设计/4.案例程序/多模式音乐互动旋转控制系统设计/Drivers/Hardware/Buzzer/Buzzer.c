#include ".\Hardware\Buzzer\Buzzer.h"

/**
 * @file Buzzer.c
 * @brief 蜂鸣器驱动实现。
 */

/**
 * @brief 初始化蜂鸣器 GPIO，并默认关闭蜂鸣器。
 */
void Buzzer_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitStruct.Pin = BUZZER_GPIO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BUZZER_GPIO_PORT, &GPIO_InitStruct);

    Buzzer_Off();
}

/**
 * @brief 打开蜂鸣器。
 * @note 当前硬件为低电平有效。
 */
void Buzzer_On(void)
{
    HAL_GPIO_WritePin(BUZZER_GPIO_PORT, BUZZER_GPIO_PIN, GPIO_PIN_RESET);
}

/**
 * @brief 关闭蜂鸣器。
 */
void Buzzer_Off(void)
{
    HAL_GPIO_WritePin(BUZZER_GPIO_PORT, BUZZER_GPIO_PIN, GPIO_PIN_SET);
}
