#ifndef __DHT11_H
#define __DHT11_H

#include "stm32f1xx_hal.h" // 根据你的芯片型号修改
#include ".\SYSTEM\delay\delay.h" // 包含延时函数头文件

/* ==================== 引脚定义 ==================== */
#define DHT11_Data_PORT GPIOA
#define DHT11_Data_PIN GPIO_PIN_2

/* ==================== 数据结构 ==================== */
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
uint8_t DHT11_Init(void);
uint8_t DHT11_GetTemperature(void);
uint8_t DHT11_GetHumidity(void);

#endif /* __DHT11_H */
