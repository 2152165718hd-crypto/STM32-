#include ".\Hardware\AV_Alarm\AV_Alarm.h"

void AV_Alarm_Init(void)
{
    // 初始化报警系统硬件
    // 例如配置GPIO引脚，定时器等
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = ALARM_GPIO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(ALARM_GPIO_PORT, &GPIO_InitStruct);
    // 默认关闭报警
    HAL_GPIO_WritePin(ALARM_GPIO_PORT, ALARM_GPIO_PIN, GPIO_PIN_SET);
}



void AV_Alarm_On(void)
{
    // 打开报警
    HAL_GPIO_WritePin(ALARM_GPIO_PORT, ALARM_GPIO_PIN, GPIO_PIN_RESET);
}

void AV_Alarm_Off(void)
{
    // 关闭报警
    HAL_GPIO_WritePin(ALARM_GPIO_PORT, ALARM_GPIO_PIN, GPIO_PIN_SET);
}

uint8_t Get_AV_Alarm_State(void)
{
    // 获取当前报警状态
    GPIO_PinState pinState = HAL_GPIO_ReadPin(ALARM_GPIO_PORT, ALARM_GPIO_PIN);
    return (pinState == GPIO_PIN_RESET) ? 1 : 0; // 根据实际情况返回1或0
}
