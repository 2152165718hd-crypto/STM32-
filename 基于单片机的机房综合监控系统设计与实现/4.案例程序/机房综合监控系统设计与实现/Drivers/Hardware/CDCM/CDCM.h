#ifndef __CDCM_H
#define __CDCM_H

#include "stm32f1xx_hal.h"

#define CDCM_AO_GPIO_PORT GPIOA
#define CDCM_AO_GPIO_PIN GPIO_PIN_4
#define CDCM_DO_GPIO_PORT GPIOB
#define CDCM_DO_GPIO_PIN GPIO_PIN_9

#define CDCM_ADC_CHANNEL ADC_CHANNEL_4

#define CDCM_ADC_VREF 3.3f
#define CDCM_ADC_MAX 4095.0f

/* WCS1800-35A 分辨率: 66mV/A */
#define CDCM_SENSITIVITY_V_PER_A 0.066f

/* 0A 对应电压（可按实际标定微调） */
#define CDCM_ZERO_CURRENT_VOLTAGE 2.5f

/* 本模块电阻分压: ADC电压 = AO电压 * (27/37) */
#define CDCM_AO_DIVIDER_RATIO (27.0f / 37.0f)

void CDCM_Init(void);
uint8_t CDCM_GetDO(void);
uint16_t CDCM_GetAO_Raw(void);
float CDCM_GetADCVoltage(void);
float CDCM_GetSensorVoltage(void);
float CDCM_GetCurrentA(void);
float CDCM_GetCurrentAbsA(void);

#endif /* __CDCM_H */