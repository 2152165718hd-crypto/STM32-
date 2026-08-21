#ifndef __DHT11_H
#define __DHT11_H

/**
 * @file DHT11.h
 * @brief DHT11 温湿度传感器驱动接口声明。
 */

#include "stm32f1xx_hal.h"
#include ".\SYSTEM\delay\delay.h"

/* ==================== 引脚定义 ==================== */
#define DHT11_Data_PORT GPIOB
#define DHT11_Data_PIN GPIO_PIN_11

/* ==================== 数据结构 ==================== */
/** @brief DHT11 最近一次读取到的数据。 */
typedef struct
{
    uint8_t humi_int;  // 湿度整数部分
    uint8_t humi_deci; // 湿度小数部分
    uint8_t temp_int;  // 温度整数部分
    uint8_t temp_deci; // 温度小数部分
    float humidity;    // 湿度值
    float temperature; // 温度值
} DHT11_Data_t;

/* ==================== 函数声明 ==================== */
/**
 * @brief 初始化 DHT11 传感器并检测是否在线。
 * @return 检测成功返回 `0`，失败返回非零。
 */
uint8_t DHT11_Init(void);

/**
 * @brief 读取温度整数值。
 * @return 温度整数部分。
 */
uint8_t DHT11_GetTemperature(void);

/**
 * @brief 读取湿度整数值。
 * @return 湿度整数部分。
 */
uint8_t DHT11_GetHumidity(void);

#endif /* __DHT11_H */
