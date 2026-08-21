#include ".\Hardware\Vibrator\Vibrator.h"

void Vibrator_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitStruct.Pin = VIBRATOR_GPIO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP; // 根据实际情况选择上拉
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(VIBRATOR_GPIO_PORT, &GPIO_InitStruct);
    Vibrator_Off(); // 初始化时关闭震动器
}

void Vibrator_On(void)
{
    HAL_GPIO_WritePin(VIBRATOR_GPIO_PORT, VIBRATOR_GPIO_PIN, GPIO_PIN_SET);
}

void Vibrator_Off(void)
{
    HAL_GPIO_WritePin(VIBRATOR_GPIO_PORT, VIBRATOR_GPIO_PIN, GPIO_PIN_RESET);
}
