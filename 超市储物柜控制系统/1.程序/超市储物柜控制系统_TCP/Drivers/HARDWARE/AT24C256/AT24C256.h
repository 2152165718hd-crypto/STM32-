#ifndef __AT24C256_H
#define __AT24C256_H

#include "stm32f4xx_hal.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AT24C256_I2C I2C2
#define AT24C256_I2C_CLK_ENABLE() __HAL_RCC_I2C2_CLK_ENABLE()
#define AT24C256_I2C_GPIO_CLK_ENABLE() __HAL_RCC_GPIOB_CLK_ENABLE()
#define AT24C256_I2C_SCL_PORT GPIOB
#define AT24C256_I2C_SCL_PIN GPIO_PIN_10
#define AT24C256_I2C_SDA_PORT GPIOB
#define AT24C256_I2C_SDA_PIN GPIO_PIN_11
#define AT24C256_I2C_AF GPIO_AF4_I2C2

#define AT24C256_ADDR_7BIT 0x50U
#define AT24C256_PAGE_SIZE 64U
#define AT24C256_TOTAL_SIZE 32768U

void AT24C256_Init(void);
HAL_StatusTypeDef AT24C256_IsReady(void);
HAL_StatusTypeDef AT24C256_WriteByte(uint16_t mem_addr, uint8_t data);
HAL_StatusTypeDef AT24C256_ReadByte(uint16_t mem_addr, uint8_t *data);
HAL_StatusTypeDef AT24C256_Write(uint16_t mem_addr, const uint8_t *data, uint16_t len);
HAL_StatusTypeDef AT24C256_Read(uint16_t mem_addr, uint8_t *data, uint16_t len);
I2C_HandleTypeDef *AT24C256_GetI2CHandle(void);

#ifdef __cplusplus
}
#endif

#endif
