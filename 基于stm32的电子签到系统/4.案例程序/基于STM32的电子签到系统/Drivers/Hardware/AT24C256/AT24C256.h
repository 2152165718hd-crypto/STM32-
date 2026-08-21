#ifndef __AT24C256_H
#define __AT24C256_H

#include "stm32f1xx_hal.h" // 根据你的芯片型号修改

#ifdef __cplusplus
extern "C"
{
#endif

/* ---------------------- 硬件配置区 ---------------------- */
#define AT24C256_I2C_PORT GPIOA
#define AT24C256_I2C_SCL_PIN GPIO_PIN_1
#define AT24C256_I2C_SDA_PIN GPIO_PIN_0

/* ---------------------- 器件参数 ---------------------- */
#define AT24C256_ADDR_7BIT 0x50u
#define AT24C256_PAGE_SIZE 64u
#define AT24C256_TOTAL_SIZE 32768u

    /* ---------------------- 对外接口 ---------------------- */
    void AT24C256_Init(void);

    HAL_StatusTypeDef AT24C256_IsReady(void);

    HAL_StatusTypeDef AT24C256_WriteByte(uint16_t mem_addr, uint8_t data);
    HAL_StatusTypeDef AT24C256_ReadByte(uint16_t mem_addr, uint8_t *data);

    HAL_StatusTypeDef AT24C256_Write(uint16_t mem_addr, const uint8_t *data, uint16_t len);
    HAL_StatusTypeDef AT24C256_Read(uint16_t mem_addr, uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* __AT24C256_H */
