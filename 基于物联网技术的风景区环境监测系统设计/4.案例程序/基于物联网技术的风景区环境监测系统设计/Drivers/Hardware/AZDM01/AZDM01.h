#ifndef __AZDM01_H
#define __AZDM01_H

#include "stm32f1xx_hal.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
	AZDM01_NTU_CLEAR = 0,
	AZDM01_NTU_500,
	AZDM01_NTU_1000,
	AZDM01_NTU_2000,
	AZDM01_NTU_4000,
	AZDM01_NTU_OVER_RANGE
} AZDM01_NTUGrade_t;

typedef struct
{
	ADC_HandleTypeDef *hadc;
	TIM_HandleTypeDef *htimPwm;
	uint32_t pwmChannel;
	GPIO_TypeDef *digitalPort;
	uint16_t digitalPin;

	float vref;              /* ADC 参考电压，默认 3.3V */
	float adcFullScale;      /* ADC 满量程，12 位默认 4095 */
	float calibVoltage;      /* 校准点电压 U0，默认 3.8V */
	float calibDutyPercent;  /* 校准时 PWM 占空比，便于保存 */
	float tempC;             /* 温度补偿参数，默认 25C */
	float kValue;            /* Arduino 线性换算截距，默认 3347.19 */
	float ntuMax;            /* 最大输出 NTU，默认 3000 */
} AZDM01_HandleTypeDef;

typedef struct
{
	uint16_t adcRaw;
	float voltage;
	float factor;                /* f = Um / U0 */
	float factorPercent;         /* f * 100 */
	uint16_t ntuEstimate;
	AZDM01_NTUGrade_t ntuGrade;
} AZDM01_ResultTypeDef;

#define AZDM01_DEFAULT_DIGITAL_PORT    GPIOA
#define AZDM01_DEFAULT_DIGITAL_PIN     GPIO_PIN_6

HAL_StatusTypeDef AZDM01_Init(AZDM01_HandleTypeDef *hazdm,
							  ADC_HandleTypeDef *hadc,
							  TIM_HandleTypeDef *htimPwm,
							  uint32_t pwmChannel);

void AZDM01_SetCalibrationVoltage(AZDM01_HandleTypeDef *hazdm, float u0Voltage);
void AZDM01_SetVref(AZDM01_HandleTypeDef *hazdm, float vrefVoltage);
void AZDM01_SetTemperature(AZDM01_HandleTypeDef *hazdm, float tempC);
void AZDM01_SetKValue(AZDM01_HandleTypeDef *hazdm, float kValue);
void AZDM01_SetNtuMax(AZDM01_HandleTypeDef *hazdm, float ntuMax);

HAL_StatusTypeDef AZDM01_SetPwmDuty(AZDM01_HandleTypeDef *hazdm, float dutyPercent);
HAL_StatusTypeDef AZDM01_DigitalInputInit(AZDM01_HandleTypeDef *hazdm,
										GPIO_TypeDef *port,
										uint16_t pin);
HAL_StatusTypeDef AZDM01_DigitalInputInitDefaultPA6(AZDM01_HandleTypeDef *hazdm);
GPIO_PinState AZDM01_ReadDigital(AZDM01_HandleTypeDef *hazdm);
HAL_StatusTypeDef AZDM01_ReadRaw(AZDM01_HandleTypeDef *hazdm, uint16_t *raw);
HAL_StatusTypeDef AZDM01_ReadVoltage(AZDM01_HandleTypeDef *hazdm, uint8_t samples, float *voltage);

HAL_StatusTypeDef AZDM01_SaveCalibrationFromCurrent(AZDM01_HandleTypeDef *hazdm,
													float currentDutyPercent,
													uint8_t samples);

float AZDM01_GetFactor(AZDM01_HandleTypeDef *hazdm, float measuredVoltage);
AZDM01_NTUGrade_t AZDM01_ClassifyNTU(float factorPercent);
uint16_t AZDM01_EstimateNTU(float factorPercent);
uint16_t AZDM01_EstimateNTUByVoltage(AZDM01_HandleTypeDef *hazdm, float measuredVoltage);
AZDM01_NTUGrade_t AZDM01_ClassifyNTUByValue(uint16_t ntuValue);

HAL_StatusTypeDef AZDM01_Measure(AZDM01_HandleTypeDef *hazdm,
								 uint8_t samples,
								 AZDM01_ResultTypeDef *result);

#ifdef __cplusplus
}
#endif

#endif
