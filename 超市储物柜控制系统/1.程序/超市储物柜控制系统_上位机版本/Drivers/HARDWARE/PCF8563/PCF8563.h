#ifndef __PCF8563_H
#define __PCF8563_H

#include "stm32f4xx_hal.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PCF8563_I2C I2C1
#define PCF8563_I2C_CLK_ENABLE() __HAL_RCC_I2C1_CLK_ENABLE()
#define PCF8563_I2C_GPIO_CLK_ENABLE() __HAL_RCC_GPIOB_CLK_ENABLE()
#define PCF8563_I2C_SCL_GPIO_CLK_ENABLE() __HAL_RCC_GPIOB_CLK_ENABLE()
#define PCF8563_I2C_SDA_GPIO_CLK_ENABLE() __HAL_RCC_GPIOB_CLK_ENABLE()
#define PCF8563_I2C_SCL_PORT GPIOB
#define PCF8563_I2C_SCL_PIN GPIO_PIN_8
#define PCF8563_I2C_SDA_PORT GPIOB
#define PCF8563_I2C_SDA_PIN GPIO_PIN_9
#define PCF8563_CLKOUT_PORT GPIOD
#define PCF8563_CLKOUT_PIN GPIO_PIN_3
#define PCF8563_INT_PORT GPIOC
#define PCF8563_INT_PIN GPIO_PIN_9
#define PCF8563_I2C_AF GPIO_AF4_I2C1
#define PCF8563_ADDR_7BIT 0x51U

typedef enum
{
    PCF8563_I2C_MODE_SOFT = 0U,
    PCF8563_I2C_MODE_HARD = 1U
} PCF8563_I2C_Mode_t;

#define PCF8563_I2C_MODE_DEFAULT PCF8563_I2C_MODE_SOFT

#define PCF8563_SOFT_I2C_SCL_GPIO_CLK_ENABLE() PCF8563_I2C_SCL_GPIO_CLK_ENABLE()
#define PCF8563_SOFT_I2C_SDA_GPIO_CLK_ENABLE() PCF8563_I2C_SDA_GPIO_CLK_ENABLE()
#define PCF8563_SOFT_I2C_SCL_PORT PCF8563_I2C_SCL_PORT
#define PCF8563_SOFT_I2C_SCL_PIN PCF8563_I2C_SCL_PIN
#define PCF8563_SOFT_I2C_SDA_PORT PCF8563_I2C_SDA_PORT
#define PCF8563_SOFT_I2C_SDA_PIN PCF8563_I2C_SDA_PIN
#define PCF8563_SOFT_I2C_DELAY_US 5U
#define PCF8563_SOFT_I2C_TIMEOUT 1000U

typedef struct
{
    uint8_t year;
    uint8_t month;
    uint8_t day;
    uint8_t weekday;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t valid;
} PCF8563_Time_t;

void PCF8563_Init(void);
void PCF8563_SetI2CMode(PCF8563_I2C_Mode_t mode);
PCF8563_I2C_Mode_t PCF8563_GetI2CMode(void);
HAL_StatusTypeDef PCF8563_IsReady(void);
HAL_StatusTypeDef PCF8563_GetTime(PCF8563_Time_t *time);
HAL_StatusTypeDef PCF8563_SetTime(const PCF8563_Time_t *time);
I2C_HandleTypeDef *PCF8563_GetI2CHandle(void);

void P8563_init(void);
void P8563_gettime(void);

#ifdef __cplusplus
}
#endif

#endif
