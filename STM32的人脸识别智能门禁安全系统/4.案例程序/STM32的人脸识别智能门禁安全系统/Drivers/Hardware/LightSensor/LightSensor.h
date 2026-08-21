#ifndef __LIGHTSENSOR_H
#define __LIGHTSENSOR_H

#include "stm32f1xx_hal.h"        // 根据你的芯片型号修改
#include ".\SYSTEM\delay\delay.h" // 包含延时函数头文件
#include <stdint.h>
/* ==================== 引脚定义 ==================== */
#define LIGHTSENSOR_Data_PORT GPIOA
#define LIGHTSENSOR_Analog_Pin GPIO_PIN_5

/* 对应 ADC 通道（PA5 -> ADC_CHANNEL_5 for STM32F1） */
#define LIGHTSENSOR_ADC_CHANNEL ADC_CHANNEL_5

/* 函数原型
 * 提供：
 * - 一个初始化函数 `LightSensor_Init()`，返回是否成功（1 成功，0 失败）
 * - 两个读取函数：`LightSensor_ReadDigital()` 和 `LightSensor_ReadAnalog()`（无需传入 ADC 句柄）
 */
uint8_t LightSensor_Init(void);
uint16_t LightSensor_ReadAnalog(void);

#endif /* __LIGHTSENSOR_H */
