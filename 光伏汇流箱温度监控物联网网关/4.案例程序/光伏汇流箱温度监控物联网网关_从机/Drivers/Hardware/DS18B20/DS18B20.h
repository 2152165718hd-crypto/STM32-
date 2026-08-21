#ifndef DS18B20_H
#define DS18B20_H

#include "stm32f1xx_hal.h"

/* ---- 引脚定义（按实际硬件修改） ---- */
#define DS18B20_PORT GPIOB
#define DS18B20_PIN GPIO_PIN_0
#define DS18B20_GPIO_CLK_EN() __HAL_RCC_GPIOB_CLK_ENABLE()

/* 读取失败时的返回值（不会与正常温度混淆） */
#define DS18B20_TEMP_ERROR (-999.0f)

/**
 * @brief  初始化 DS18B20 引脚
 * @retval 1-传感器在线  0-未检测到传感器
 */
uint8_t DS18B20_Init(void);

/**
 * @brief  读取温度（阻塞约 750 ms）
 * @retval 温度值(℃)，失败返回 DS18B20_TEMP_ERROR
 */
float DS18B20_ReadTemperature(void);

#endif /* DS18B20_H */
