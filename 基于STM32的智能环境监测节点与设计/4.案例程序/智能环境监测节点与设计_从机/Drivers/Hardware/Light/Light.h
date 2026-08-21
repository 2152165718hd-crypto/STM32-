#ifndef __LIGHT_H__
#define __LIGHT_H__

#include "stm32f1xx_hal.h"

/* ==================== 地址定义(7-bit) ==================== */
#define BH1750_I2C_HIGH_ADDR 0x5CU
#define BH1750_I2C_LOW_ADDR 0x23U

/* 保留旧宏名，避免已有代码因拼写问题受影响 */
#define BH1750_I2C_HIGHT_ADDR BH1750_I2C_HIGH_ADDR

/* ==================== 模式选择 ==================== */
#define BH1750_USE_HARDWARE_I2C 0U /* 1: 硬件I2C, 0: 软件I2C */

/* ==================== 引脚定义 ==================== */
#define BH1750_I2C_SCL_PIN GPIO_PIN_7
#define BH1750_I2C_SCL_PORT GPIOA

#define BH1750_I2C_SDA_PIN GPIO_PIN_6
#define BH1750_I2C_SDA_PORT GPIOA

#define BH1750_I2C_ADDR_PIN GPIO_PIN_5
#define BH1750_I2C_ADDR_PORT GPIOA

/* 兼容旧宏 */
#define BH1750_I2C_PORT BH1750_I2C_SCL_PORT

/* ==================== 地址选择 ==================== */
#define BH1750_ADDR_SELECT_BY_PIN 1U /* 1: 读取ADDR引脚电平, 0: 使用固定地址 */
#define BH1750_FIXED_ADDR BH1750_I2C_LOW_ADDR

/* ==================== 硬件I2C参数 ==================== */
#define BH1750_HARDWARE_I2C_INDEX 1U /* 1: I2C1, 2: I2C2 */
#define BH1750_HARDWARE_I2C_REMAP 1U /* I2C1重映射到PB8/PB9 */
#define BH1750_I2C_CLOCK_SPEED 100000U
#define BH1750_I2C_TIMEOUT 100U

/* ==================== 软件I2C参数 ==================== */
#define BH1750_SOFT_I2C_DELAY_US 5U

/* ==================== BH1750命令 ==================== */
#define BH1750_CMD_POWER_DOWN 0x00U
#define BH1750_CMD_POWER_ON 0x01U
#define BH1750_CMD_RESET 0x07U

#define BH1750_CMD_CONT_H_RES_MODE 0x10U
#define BH1750_CMD_CONT_H_RES2_MODE 0x11U
#define BH1750_CMD_CONT_L_RES_MODE 0x13U
#define BH1750_CMD_ONE_H_RES_MODE 0x20U
#define BH1750_CMD_ONE_H_RES2_MODE 0x21U
#define BH1750_CMD_ONE_L_RES_MODE 0x23U

#define BH1750_DEFAULT_MODE BH1750_CMD_CONT_H_RES_MODE
#define BH1750_MEASURE_DELAY_H_RES_MS 180U
#define BH1750_MEASURE_DELAY_L_RES_MS 24U

/* ==================== 返回值 ==================== */
#define BH1750_OK 0U
#define BH1750_ERR_PARAM 1U
#define BH1750_ERR_I2C_ACK 2U
#define BH1750_ERR_TIMEOUT 3U

/* ==================== 函数声明 ==================== */
uint8_t BH1750_Init(void);
uint8_t BH1750_SetMode(uint8_t mode);
uint8_t BH1750_ReadRaw(uint16_t *raw_data);
uint8_t BH1750_ReadLux(float *lux);
float BH1750_GetLux(void);

#endif /* __LIGHT_H__ */
