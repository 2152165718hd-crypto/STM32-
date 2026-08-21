#ifndef __MQ135_SMOKE_H__
#define __MQ135_SMOKE_H__

#include "stm32f1xx_hal.h"

/* Pin definitions */
#define MQ135_SMOKE_DO_PORT GPIOA
#define MQ135_SMOKE_DO_PIN GPIO_PIN_0

#define MQ135_SMOKE_AO_PORT GPIOA
#define MQ135_SMOKE_AO_PIN GPIO_PIN_1

#define MQ135_SMOKE_CHANNEL ADC_CHANNEL_1

/* ADC 分压系数：ADC 采样值 = 模块实际 AO 输出 * 27/37 */
#define MQ135_SMOKE_AO_DIVIDER_RATIO (27.0f / 37.0f)
#define MQ135_SMOKE_ADC_REF_VOLTAGE 3.3f

/* Public API */
void MQ135_Smoke_Init(void);
uint8_t MQ135_Smoke_GetDO(void);
uint16_t MQ135_Smoke_GetAO_Raw(void);
float MQ135_Smoke_GetVoltage(void);

#endif /* __MQ135_SMOKE_H__ */
