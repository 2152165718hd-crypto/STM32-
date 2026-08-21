#ifndef __VL53L0X_H
#define __VL53L0X_H

#include "stm32f1xx_hal.h"

#define VL53L0X_SCL_PORT GPIOB
#define VL53L0X_SCL_PIN  GPIO_PIN_11
#define VL53L0X_SDA_PORT GPIOB
#define VL53L0X_SDA_PIN  GPIO_PIN_10
#define VL53L0X_XSHUT_PORT GPIOB
#define VL53L0X_XSHUT_PIN  GPIO_PIN_8
#define VL53L0X_GPIO1_PORT GPIOB
#define VL53L0X_GPIO1_PIN  GPIO_PIN_9
#define VL53L0X_GPIO1_ACTIVE_HIGH 1u

#define VL53L0X_HSHUT_PORT VL53L0X_XSHUT_PORT
#define VL53L0X_HSHUT_PIN  VL53L0X_XSHUT_PIN

#define VL53L0X_DISTANCE_INVALID 0xFFFFu

uint8_t VL53L0X_Init(void);
uint8_t VL53L0X_IsReady(void);
uint16_t VL53L0X_ReadDistanceMm(void);
uint8_t VL53L0X_IsBlocked(uint16_t threshold_mm);
void VL53L0X_SetXshut(uint8_t level);
uint8_t VL53L0X_GetXshut(void);
uint8_t VL53L0X_Gpio1_IsActive(void);

#endif /* __VL53L0X_H */
