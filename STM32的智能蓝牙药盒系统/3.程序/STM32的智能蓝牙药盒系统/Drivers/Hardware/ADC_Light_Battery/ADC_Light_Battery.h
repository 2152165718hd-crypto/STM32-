#ifndef __ADC_LIGHT_BATTERY_H
#define __ADC_LIGHT_BATTERY_H

/**
 * @file ADC_Light_Battery.h
 * @brief 光照与电池电压采样驱动接口声明。
 */

#include "stm32f1xx_hal.h"
#include ".\SYSTEM\delay\delay.h"
#include <stdint.h>

/* ==================== 引脚定义 ==================== */
#define LIGHTSENSOR_Data_PORT GPIOA
#define LIGHTSENSOR_Analog_Pin GPIO_PIN_1

#define BATTERY_Data_PORT GPIOA
#define BATTERY_Analog_Pin GPIO_PIN_0

/* ==================== ADC 通道定义 ==================== */
#define BATTERY_ADC_CHANNEL ADC_CHANNEL_0
#define LIGHTSENSOR_ADC_CHANNEL ADC_CHANNEL_1

/* ==================== 电池参数 ==================== */
#define BATTERY_VREF 3.3f                     /* ADC 参考电压 (V) */
#define BATTERY_ADC_MAX 4095                  /* 12-bit ADC 最大值 */
#define BATTERY_DIVIDER_RATIO (37.0f / 27.0f) /* 采样比 27/37：实际电压 = ADC电压 × (37/27) */
#define BATTERY_FULL_VOLTAGE 4.2f             /* 锂电池满电电压 (V) */
#define BATTERY_EMPTY_VOLTAGE 3.0f            /* 锂电池截止电压 (V) */

/* ==================== 滤波参数 ==================== */
#define BATTERY_SAMPLE_COUNT 10    /* 每次采集多少次取中值滤波 */
#define BATTERY_FILTER_ALPHA 0.1f  /* 一阶低通滤波系数（越小越平滑） */
#define BATTERY_PERCENT_MAX_STEP 1 /* 电量百分比单次最大变化量 */

/* ==================== 函数原型 ==================== */

/**
 * @brief 初始化光照与电池共用的 ADC1 外设。
 * @return 初始化成功返回 `1`，失败返回 `0`。
 */
uint8_t ADC_Light_Battery_Init(void);

/**
 * @brief 读取光照传感器原始模拟值。
 * @return 12 位 ADC 原始值。
 */
uint16_t LightSensor_ReadAnalog(void);

/**
 * @brief 读取电池电压通道的原始 ADC 值。
 * @return 12 位 ADC 原始值。
 */
uint16_t Battery_ReadAnalog(void);

/**
 * @brief 获取滤波后的实际电池电压。
 * @return 电池电压，单位为伏特。
 */
float Battery_GetVoltage(void);

/**
 * @brief 估算当前电量百分比。
 * @return 电量百分比，范围 `0~100`。
 */
uint8_t Battery_GetPercent(void);

#endif /* __ADC_LIGHT_BATTERY_H */
