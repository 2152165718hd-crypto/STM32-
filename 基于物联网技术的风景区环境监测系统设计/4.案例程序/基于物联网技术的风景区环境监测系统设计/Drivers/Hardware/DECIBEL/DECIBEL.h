#ifndef __DECIBEL_H
#define __DECIBEL_H

#include "stm32f1xx_hal.h"

/* DECIBEL 模块接线：AO -> PA7(ADC1_IN7) */
#define DECIBEL_ADC_INSTANCE ADC1
#define DECIBEL_ADC_CHANNEL ADC_CHANNEL_7
#define DECIBEL_GPIO_PORT GPIOA
#define DECIBEL_GPIO_PIN GPIO_PIN_7

/* STM32F103C8 ADC 量化参数 */
#define DECIBEL_ADC_MAX 4095u
#define DECIBEL_VREF_MV 3300u

/* 模块模拟输出上限约 2.3V，用于换算显示百分比 */
#define DECIBEL_OUTPUT_MAX_MV 2300u

/* 默认平均采样点数 */
#define DECIBEL_DEFAULT_SAMPLES 16u

void Decibel_Init(void);
uint16_t Decibel_ReadRaw(void);
uint16_t Decibel_ReadRawAverage(uint8_t samples);
uint32_t Decibel_RawToMilliVolt(uint16_t raw);
uint32_t Decibel_ReadMilliVolt(void);
uint8_t Decibel_GetPercent(uint16_t raw);
uint8_t Decibel_ReadPercent(void);

#endif
