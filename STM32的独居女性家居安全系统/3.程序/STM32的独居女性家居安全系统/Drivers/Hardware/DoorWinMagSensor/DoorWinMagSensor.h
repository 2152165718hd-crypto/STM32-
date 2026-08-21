#ifndef __DOORWINMAGSENSOR_H
#define __DOORWINMAGSENSOR_H

#include "stm32f1xx_hal.h"

#define DOORWIN_MAG_SENSOR_GPIO_PORT GPIOA
#define DOORWIN_MAG_SENSOR_GPIO_PIN  GPIO_PIN_1


//常闭型磁窗磁传感器，磁铁靠近传感器时断开电路，磁铁远离传感器时闭合电路。
//IO口上拉输入，磁铁靠近传感器时（闭合）IO口读取到1，磁铁远离传感器时（打开）IO口读取到0。
void DoorWinMagSensor_Init(void);

// 返回值：1表示磁铁靠近传感器（闭合），0表示磁铁远离传感器（打开）
uint8_t DoorWinMagSensor_Read(void);

#endif /* __DOORWINMAGSENSOR_H */

