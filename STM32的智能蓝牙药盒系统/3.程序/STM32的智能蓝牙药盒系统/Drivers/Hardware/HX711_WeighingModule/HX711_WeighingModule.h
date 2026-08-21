#ifndef __HX711_WEIGHING_MODULE_H__
#define __HX711_WEIGHING_MODULE_H__

/**
 * @file HX711_WeighingModule.h
 * @brief HX711 称重模块驱动接口声明。
 */

#include "stm32f1xx_hal.h"

/* 引脚定义 */
#define HX711_SCK_GPIO_PORT GPIOB
#define HX711_SCK_PIN GPIO_PIN_1
#define HX711_DOUT_GPIO_PORT GPIOB
#define HX711_DOUT_PIN GPIO_PIN_0

/* HX711 增益与通道选择 */
#define HX711_GAIN_128 25 /* 通道A, 增益128 */
#define HX711_GAIN_64 27  /* 通道A, 增益64  */
#define HX711_GAIN_32 26  /* 通道B, 增益32  */

/* 函数声明 */
/**
 * @brief 初始化 HX711 GPIO。
 */
void HX711_Init(void);

/**
 * @brief 读取 HX711 原始 24 位采样值。
 * @return 原始 ADC 数据。
 */
uint32_t HX711_ReadRaw(void);

/**
 * @brief 对当前秤体进行去皮。
 * @param times 去皮采样次数。
 */
void HX711_Tare(uint8_t times);

/**
 * @brief 获取当前重量。
 * @return 重量值，单位为克。
 */
float HX711_GetWeight(void);

/**
 * @brief 设置称重校准系数。
 * @param factor 新的校准系数。
 */
void HX711_SetCalibration(float factor);

#endif /* __HX711_WEIGHING_MODULE_H__ */
