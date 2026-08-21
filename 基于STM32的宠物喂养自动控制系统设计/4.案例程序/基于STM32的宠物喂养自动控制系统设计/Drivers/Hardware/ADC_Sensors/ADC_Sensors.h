#ifndef __ADC_SENSORS_H__
#define __ADC_SENSORS_H__

/**
 * @file ADC_Sensors.h
 * @brief ADC 传感器模块接口声明（光照/水位/MQ135）。
 */

#include "stm32f1xx_hal.h"

#define LightSensor_ADC_PORT GPIOA
#define LightSensor_ADC_PIN GPIO_PIN_4
#define LightSensor_ADC_CHANNEL ADC_CHANNEL_4

#define Water_ADC_PORT GPIOA
#define Water_ADC_PIN GPIO_PIN_5
#define Water_ADC_CHANNEL ADC_CHANNEL_5

// MQ135 是一个常用的空气质量传感器，输出模拟电压与空气中污染物浓度相关。
//最高浓度输出4V,读取到的电压为实际模块输出电压的27/37。
#define MQ135_ADC_PORT GPIOA
#define MQ135_ADC_PIN GPIO_PIN_7
#define MQ135_DO_PORT GPIOA
#define MQ135_DO_PIN GPIO_PIN_6
#define MQ135_ADC_CHANNEL ADC_CHANNEL_7

/* ==================== ADC 参数配置 ==================== */
#define ADC_SENSORS_VREF_MV 3300U
#define ADC_SENSORS_MAX_VALUE 4095U
#define ADC_SENSORS_SAMPLE_TIME ADC_SAMPLETIME_239CYCLES_5
#define ADC_SENSORS_TIMEOUT_MS 10U
#define ADC_SENSORS_DEFAULT_SAMPLES 8U

/* MQ135 模块分压补偿：Vout = Vadc * (37 / 27) */
#define MQ135_VOLTAGE_RATIO_NUM 37.0f
#define MQ135_VOLTAGE_RATIO_DEN 27.0f

/**
 * @brief 初始化 ADC 传感器模块。
 * @note 会初始化 ADC1、模拟输入引脚和 MQ135 数字输出引脚。
 */
void ADC_Sensors_Init(void);

/**
 * @brief 读取指定 ADC 通道的平均原始值。
 * @param channel ADC 通道号（如 ADC_CHANNEL_4）。
 * @return 12 位原始采样值（0~4095）。
 */
uint16_t ADC_Sensors_ReadChannelRaw(uint32_t channel);

/**
 * @brief 读取指定 ADC 通道电压（单位 V）。
 * @param channel ADC 通道号。
 * @return 采样电压值。
 */
float ADC_Sensors_ReadChannelVoltage(uint32_t channel);

/**
 * @brief 读取光照传感器 ADC 原始值。
 * @return 12 位原始值。
 */
uint16_t LightSensor_ReadRaw(void);

/**
 * @brief 读取光照传感器电压（单位 V）。
 * @return 电压值。
 */
float LightSensor_ReadVoltage(void);

/**
 * @brief 读取水位传感器 ADC 原始值。
 * @return 12 位原始值。
 */
uint16_t WaterSensor_ReadRaw(void);

/**
 * @brief 读取水位传感器电压（单位 V）。
 * @return 电压值。
 */
float WaterSensor_ReadVoltage(void);

/**
 * @brief 读取 MQ135 ADC 原始值。
 * @return 12 位原始值。
 */
uint16_t MQ135_ReadRaw(void);

/**
 * @brief 读取 MQ135 采样电压（ADC 引脚实测，单位 V）。
 * @return 电压值。
 */
float MQ135_ReadVoltage(void);

/**
 * @brief 读取 MQ135 模块输出电压（按 37/27 分压系数反推，单位 V）。
 * @return 模块输出端电压值。
 */
float MQ135_ReadOutputVoltage(void);

/**
 * @brief 读取 MQ135 DO 数字口电平。
 * @return GPIO 电平值，`0` 或 `1`。
 */
uint8_t MQ135_ReadDO(void);

#endif
