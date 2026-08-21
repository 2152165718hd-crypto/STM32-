#include ".\Hardware\SR505\SR505.h"

// 红外传感器初始化函数
void SR505_Init(void)
{
    // 初始化红外传感器硬件
    // 例如配置GPIO引脚，定时器等
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = SR505_GPIO_PIN; // 使用定义的引脚
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP; // 根据实际情况选择上拉或下拉
    HAL_GPIO_Init(SR505_GPIO_PORT, &GPIO_InitStruct);
}

// 红外传感器读取函数
// 返回值：1表示有人经过，0表示无人经过
// 有人经过时，SR505输出高电平，人走后延时约2秒后输出低电平。
uint8_t SR505_Read(void)
{
    // 读取红外传感器状态
    GPIO_PinState pinState = HAL_GPIO_ReadPin(SR505_GPIO_PORT, SR505_GPIO_PIN);
    return (pinState == GPIO_PIN_SET) ? 1 : 0; // 根据实际情况返回1或0
}
