#ifndef __WATER_LEVEL_H
#define __WATER_LEVEL_H

#include "stm32f1xx_hal.h"
#include <stdint.h>

/* 水位传感器模拟输出引脚：S -> PA6(ADC1_IN6) */
#define WATER_LEVEL_PORT GPIOA
#define WATER_LEVEL_PIN  GPIO_PIN_6

/* 百分比换算范围，默认覆盖 12bit ADC 全量程 */
#define WATER_LEVEL_ADC_MIN 0u
#define WATER_LEVEL_ADC_MAX 4095u

void WaterLevel_Init(void);

/* 读取单次 ADC 原始值(0~4095) */
uint16_t WaterLevel_ReadRaw(void);

/* 多次采样求均值，samples 传 0 时默认按 1 次处理 */
uint16_t WaterLevel_ReadRawAvg(uint8_t samples);

/* 将原始值映射为 0~100% */
uint8_t WaterLevel_ReadPercent(void);

/* 判断是否达到报警阈值（单位：%） */
uint8_t WaterLevel_IsAlarm(uint8_t threshold_percent);

#endif /* __WATER_LEVEL_H */
