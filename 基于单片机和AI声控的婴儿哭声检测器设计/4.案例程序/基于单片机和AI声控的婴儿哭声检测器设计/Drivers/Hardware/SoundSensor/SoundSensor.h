#ifndef __SOUND_SENSOR_H
#define __SOUND_SENSOR_H

#include "stm32f1xx_hal.h"        // 根据你的芯片型号修改
#include ".\SYSTEM\delay\delay.h" // 包含延时函数头文件

/* ==================== 引脚定义 ==================== */
#define SOUND_SENSOR_GPIO_PORT GPIOA
#define SOUND_SENSOR_AO_PIN GPIO_PIN_1
#define SOUND_SENSOR_DO_PIN GPIO_PIN_0
#define SOUND_SENSOR_ADC_CHANNEL ADC_CHANNEL_1
#define SOUND_SENSOR_DO_TRIGGER_LEVEL GPIO_PIN_RESET /* 常见 LM393 模块为低电平触发 */

/* 函数原型
 * 提供：
 * - 初始化函数 `SoundSensor_Init()`
 * - 模拟量读取函数 `SoundSensor_ReadAnalog()`
 * - 模拟量平均值读取函数 `SoundSensor_ReadAnalogAverage()`
 * - 数字量读取函数 `SoundSensor_ReadDigital()`
 * - 触发判断函数 `SoundSensor_IsTriggered()`（默认低电平触发）
 */

void SoundSensor_Init(void);

uint16_t SoundSensor_ReadAnalog(void);
uint16_t SoundSensor_ReadAnalogAverage(uint8_t sampleCount, uint16_t sampleIntervalMs);

uint8_t SoundSensor_ReadDigital(void);
uint8_t SoundSensor_IsTriggered(void);

#endif /* __SOUND_SENSOR_H */
