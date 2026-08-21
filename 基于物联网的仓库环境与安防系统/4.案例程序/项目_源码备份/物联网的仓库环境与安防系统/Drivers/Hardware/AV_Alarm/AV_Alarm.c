#include ".\Hardware\AV_Alarm\AV_Alarm.h"

void AV_Alarm_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitStruct.Pin = ALARM_GPIO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(ALARM_GPIO_PORT, &GPIO_InitStruct);

    HAL_GPIO_WritePin(ALARM_GPIO_PORT, ALARM_GPIO_PIN, GPIO_PIN_SET);
}

void AV_Alarm_On(void)
{
    HAL_GPIO_WritePin(ALARM_GPIO_PORT, ALARM_GPIO_PIN, GPIO_PIN_RESET);
}

void AV_Alarm_Off(void)
{
    HAL_GPIO_WritePin(ALARM_GPIO_PORT, ALARM_GPIO_PIN, GPIO_PIN_SET);
}

uint8_t Get_AV_Alarm_State(void)
{
    GPIO_PinState pin_state = HAL_GPIO_ReadPin(ALARM_GPIO_PORT, ALARM_GPIO_PIN);
    return (pin_state == GPIO_PIN_RESET) ? 1U : 0U;
}
