#ifndef __HR_SENSOR_H
#define __HR_SENSOR_H

#include <stdint.h>
#include "stm32f1xx_hal.h"

/* 心率血氧传感器：MAX30102 */

#define HR_SENSOR_SCL_PORT GPIOB
#define HR_SENSOR_SCL_PIN GPIO_PIN_10
#define HR_SENSOR_SDA_PORT GPIOB
#define HR_SENSOR_SDA_PIN GPIO_PIN_11
#define HR_SENSOR_INT_PORT GPIOB
#define HR_SENSOR_INT_PIN GPIO_PIN_1

/* 1: 使用硬件 I2C2(PB10/PB11), 0: 使用软件 I2C(bit-bang) */
#define HR_SENSOR_USE_HARDWARE_I2C 1U

#define HR_SENSOR_MAX30102_ADDR 0x57U
#define HR_SENSOR_MAX30102_PART_ID 0x15U

/* MAX30102 寄存器地址 */
#define HR_SENSOR_REG_INTR_STATUS_1 0x00U
#define HR_SENSOR_REG_INTR_STATUS_2 0x01U
#define HR_SENSOR_REG_INTR_ENABLE_1 0x02U
#define HR_SENSOR_REG_INTR_ENABLE_2 0x03U
#define HR_SENSOR_REG_FIFO_WR_PTR 0x04U
#define HR_SENSOR_REG_OVF_COUNTER 0x05U
#define HR_SENSOR_REG_FIFO_RD_PTR 0x06U
#define HR_SENSOR_REG_FIFO_DATA 0x07U
#define HR_SENSOR_REG_FIFO_CONFIG 0x08U
#define HR_SENSOR_REG_MODE_CONFIG 0x09U
#define HR_SENSOR_REG_SPO2_CONFIG 0x0AU
#define HR_SENSOR_REG_LED1_PA 0x0CU
#define HR_SENSOR_REG_LED2_PA 0x0DU
#define HR_SENSOR_REG_PILOT_PA 0x10U
#define HR_SENSOR_REG_MULTI_LED_CTRL1 0x11U
#define HR_SENSOR_REG_MULTI_LED_CTRL2 0x12U
#define HR_SENSOR_REG_TEMP_INT 0x1FU
#define HR_SENSOR_REG_TEMP_FRAC 0x20U
#define HR_SENSOR_REG_REV_ID 0xFEU
#define HR_SENSOR_REG_PART_ID 0xFFU

#define HR_SENSOR_INVALID_RAW 0xFFFFFFFFUL

typedef struct
{
	uint32_t red;
	uint32_t ir;
} HR_SensorSample_t;

void HR_Sensor_Init(void);
uint8_t HR_Sensor_IsConnected(void);
HAL_StatusTypeDef HR_Sensor_Reset(void);
HAL_StatusTypeDef HR_Sensor_ConfigDefault(void);
HAL_StatusTypeDef HR_Sensor_SetLedCurrent(uint8_t red_pa, uint8_t ir_pa);
HAL_StatusTypeDef HR_Sensor_ReadSample(HR_SensorSample_t *sample);
HAL_StatusTypeDef HR_Sensor_ReadRegister(uint8_t reg, uint8_t *value);
HAL_StatusTypeDef HR_Sensor_WriteRegister(uint8_t reg, uint8_t value);

/* 常见 MAX30102 驱动命名兼容接口 */
void MAX30102_Init(void);
HAL_StatusTypeDef MAX30102_ReadRaw(uint32_t *red, uint32_t *ir);

#endif /* __HR_SENSOR_H */
