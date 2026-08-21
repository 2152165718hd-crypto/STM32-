#include ".\Hardware\DoorWinMagSensor\DoorWinMagSensor.h"

//常闭型磁窗磁传感器，磁铁靠近传感器时断开电路，磁铁远离传感器时闭合电路。
//IO口上拉输入，磁铁靠近传感器时（闭合）IO口读取到1，磁铁远离传感器时（打开）IO口读取到0。


/* 门窗磁传感器初始化函数 */
void DoorWinMagSensor_Init(void)
{
    // 初始化门窗磁传感器硬件
    // 例如配置GPIO引脚，定时器等
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = DOORWIN_MAG_SENSOR_GPIO_PIN; // 使用定义的引脚
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP; // 根据实际情况选择上拉或下拉
    HAL_GPIO_Init(DOORWIN_MAG_SENSOR_GPIO_PORT, &GPIO_InitStruct);
}


/* 门窗磁传感器读取函数 
* 返回值：1表示磁铁靠近传感器（闭合），0表示磁铁远离传感器（打开）
*/
uint8_t DoorWinMagSensor_Read(void)
{
    // 读取门窗磁传感器状态
    GPIO_PinState pinState = HAL_GPIO_ReadPin(DOORWIN_MAG_SENSOR_GPIO_PORT, DOORWIN_MAG_SENSOR_GPIO_PIN);
    return (pinState == GPIO_PIN_SET) ? 1 : 0; // 根据实际情况返回1或0
}

