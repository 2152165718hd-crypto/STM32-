#ifndef __CRY_SENSOR_H
#define __CRY_SENSOR_H

#include "stm32f1xx_hal.h" // 根据你的芯片型号修改

/* ---- 宏定义 ---- */
#define CRY_SENSOR_P1_GPIO_PORT GPIOB
#define CRY_SENSOR_P1_PIN GPIO_PIN_11

#define CRY_SENSOR_P2_GPIO_PORT GPIOB
#define CRY_SENSOR_P2_PIN GPIO_PIN_10

/* 待机低电平，识别到声音后输出高电平脉冲 */
#define CRY_SENSOR_TRIGGER_LEVEL GPIO_PIN_SET

/* 上升沿事件掩码 */
#define CRY_SENSOR_EVENT_NONE 0x00u
#define CRY_SENSOR_EVENT_P1_RISING 0x01u
#define CRY_SENSOR_EVENT_P2_RISING 0x02u

void CrySensor_Init(void);

uint8_t CrySensor_ReadP1(void);
uint8_t CrySensor_ReadP2(void);

uint8_t CrySensor_P1_IsTriggered(void);
uint8_t CrySensor_P2_IsTriggered(void);

/*
 * 读取并清空本次采样得到的上升沿事件。
 * 建议轮询周期小于 300ms，以免遗漏脉冲。
 */
uint8_t CrySensor_GetRisingEvents(void);

/* 用当前电平重置边沿检测状态，避免首次调用误判 */
void CrySensor_ResetEdgeState(void);

#endif /* __CRY_SENSOR_H */
