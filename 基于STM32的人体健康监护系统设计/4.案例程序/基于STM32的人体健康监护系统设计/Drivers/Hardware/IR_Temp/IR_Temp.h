#ifndef __IR_TEMP_H
#define __IR_TEMP_H

#include <stdint.h>
#include "stm32f1xx_hal.h"

/*
 * MLX90614 / GY-906 红外测温模块
 * 默认使用 PB6=SCL, PB7=SDA 的软件 SMBus 方式。
 */
#define IR_TEMP_SCL_PORT GPIOB
#define IR_TEMP_SCL_PIN GPIO_PIN_6
#define IR_TEMP_SDA_PORT GPIOB
#define IR_TEMP_SDA_PIN GPIO_PIN_7

/* 1: 使用硬件 I2C1, 0: 使用软件 SMBus */
#define IR_TEMP_USE_HARDWARE_I2C 1U

#define IR_TEMP_MLX90614_ADDR 0x5AU
#define IR_TEMP_RAM_ACCESS 0x00U
#define IR_TEMP_EEPROM_ACCESS 0x20U
#define IR_TEMP_RAM_TOBJ1 0x07U

#define IR_TEMP_INVALID_CELSIUS (-1000.0f)

void IR_Temp_Init(void);
float IR_Temp_ReadObjectCelsius(void);

/* 兼容厂商源码的接口名 */
void SMBus_Init(void);
float SMBus_ReadTemp(void);

#endif /* __IR_TEMP_H */
