#ifndef __MQ2_SMOKE_H__
#define __MQ2_SMOKE_H__

#include "stm32f1xx_hal.h"

/* ==================== 引脚定义 ==================== */
#define MQ2_SMOKE_DO_PORT GPIOA
#define MQ2_SMOKE_DO_PIN GPIO_PIN_2

#define MQ2_SMOKE_AO_PORT GPIOA
#define MQ2_SMOKE_AO_PIN GPIO_PIN_3

#define MQ2_SMOKE_CHANNEL ADC_CHANNEL_3

/* ==================== 分压系数 ====================
 * MQ-2传感器供电5V，AO输出0~5V，超过ADC 3.3V上限
 * 电阻分压网络: ADC检测电压 = AO * (27 / 37)
 * 反推实际AO电压: AO = ADC电压 * (37 / 27)
 */
#define MQ2_DIVIDER_RATIO (27.0f / 37.0f)

/* ==================== 函数声明 ==================== */
void MQ2_Smoke_Init(void);
uint8_t MQ2_Smoke_GetDO(void);
uint16_t MQ2_Smoke_GetAO_Raw(void);
float MQ2_Smoke_GetVoltage(void);
float MQ2_Smoke_GetRealVoltage(void);

#endif /* __MQ2_SMOKE_H__ */

