#include ".\Hardware\AZDM01\AZDM01.h"

#define AZDM01_TU_TEMP_COEF        (-0.0192f)
#define AZDM01_TU_SLOPE            (-865.68f)
#define AZDM01_DEFAULT_K_VALUE     (3347.19f)
#define AZDM01_DEFAULT_TEMP_C      (25.0f)
#define AZDM01_DEFAULT_NTU_MAX     (3000.0f)

static float AZDM01_ClampFloat(float v, float minV, float maxV)
{
	if (v < minV)
	{
		return minV;
	}
	if (v > maxV)
	{
		return maxV;
	}
	return v;
}

static HAL_StatusTypeDef AZDM01_EnableGpioClock(GPIO_TypeDef *port)
{
	if (port == GPIOA)
	{
		__HAL_RCC_GPIOA_CLK_ENABLE();
		return HAL_OK;
	}
	if (port == GPIOB)
	{
		__HAL_RCC_GPIOB_CLK_ENABLE();
		return HAL_OK;
	}
	if (port == GPIOC)
	{
		__HAL_RCC_GPIOC_CLK_ENABLE();
		return HAL_OK;
	}
#ifdef GPIOD
	if (port == GPIOD)
	{
		__HAL_RCC_GPIOD_CLK_ENABLE();
		return HAL_OK;
	}
#endif
#ifdef GPIOE
	if (port == GPIOE)
	{
		__HAL_RCC_GPIOE_CLK_ENABLE();
		return HAL_OK;
	}
#endif
	return HAL_ERROR;
}

static HAL_StatusTypeDef AZDM01_ReadRawAverage(AZDM01_HandleTypeDef *hazdm, uint8_t samples, uint16_t *raw)
{
	HAL_StatusTypeDef ret;
	uint32_t sum = 0u;
	uint16_t minRaw = 0xFFFFu;
	uint16_t maxRaw = 0u;
	uint8_t n = (samples == 0u) ? 1u : samples;
	uint8_t validCnt;

	if ((hazdm == NULL) || (hazdm->hadc == NULL) || (raw == NULL))
	{
		return HAL_ERROR;
	}

	for (uint8_t i = 0u; i < n; i++)
	{
		ret = HAL_ADC_Start(hazdm->hadc);
		if (ret != HAL_OK)
		{
			return ret;
		}

		ret = HAL_ADC_PollForConversion(hazdm->hadc, 10u);
		if (ret != HAL_OK)
		{
			HAL_ADC_Stop(hazdm->hadc);
			return ret;
		}

		{
			uint16_t adcValue = (uint16_t)HAL_ADC_GetValue(hazdm->hadc);
			sum += adcValue;
			if (adcValue < minRaw)
			{
				minRaw = adcValue;
			}
			if (adcValue > maxRaw)
			{
				maxRaw = adcValue;
			}
		}
		(void)HAL_ADC_Stop(hazdm->hadc);
	}

	if (n >= 5u)
	{
		sum -= minRaw;
		sum -= maxRaw;
		validCnt = (uint8_t)(n - 2u);
	}
	else
	{
		validCnt = n;
	}

	*raw = (uint16_t)(sum / validCnt);
	return HAL_OK;
}

static uint16_t AZDM01_EstimateNTUByArduinoModel(AZDM01_HandleTypeDef *hazdm, float measuredVoltage)
{
	float tuCalibrated;
	float ntu;

	if (hazdm == NULL)
	{
		return 0u;
	}

	tuCalibrated = (AZDM01_TU_TEMP_COEF * (hazdm->tempC - 25.0f)) + measuredVoltage;
	ntu = (AZDM01_TU_SLOPE * tuCalibrated) + hazdm->kValue;
	ntu = AZDM01_ClampFloat(ntu, 0.0f, hazdm->ntuMax);

	return (uint16_t)(ntu + 0.5f);
}

HAL_StatusTypeDef AZDM01_Init(AZDM01_HandleTypeDef *hazdm,
							  ADC_HandleTypeDef *hadc,
							  TIM_HandleTypeDef *htimPwm,
							  uint32_t pwmChannel)
{
	if (hazdm == NULL)
	{
		return HAL_ERROR;
	}

	hazdm->hadc = hadc;
	hazdm->htimPwm = htimPwm;
	hazdm->pwmChannel = pwmChannel;
	hazdm->digitalPort = AZDM01_DEFAULT_DIGITAL_PORT;
	hazdm->digitalPin = AZDM01_DEFAULT_DIGITAL_PIN;
	hazdm->vref = 3.3f;
	hazdm->adcFullScale = 4095.0f;
	hazdm->calibVoltage = 3.8f;
	hazdm->calibDutyPercent = 0.0f;
	hazdm->tempC = AZDM01_DEFAULT_TEMP_C;
	hazdm->kValue = AZDM01_DEFAULT_K_VALUE;
	hazdm->ntuMax = AZDM01_DEFAULT_NTU_MAX;

	if (hazdm->htimPwm != NULL)
	{
		return HAL_TIM_PWM_Start(hazdm->htimPwm, hazdm->pwmChannel);
	}

	return HAL_OK;
}

HAL_StatusTypeDef AZDM01_DigitalInputInit(AZDM01_HandleTypeDef *hazdm,
										GPIO_TypeDef *port,
										uint16_t pin)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	HAL_StatusTypeDef ret;

	if ((hazdm == NULL) || (port == NULL) || (pin == 0u))
	{
		return HAL_ERROR;
	}

	ret = AZDM01_EnableGpioClock(port);
	if (ret != HAL_OK)
	{
		return ret;
	}

	GPIO_InitStruct.Pin = pin;
	GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	HAL_GPIO_Init(port, &GPIO_InitStruct);

	hazdm->digitalPort = port;
	hazdm->digitalPin = pin;

	return HAL_OK;
}

HAL_StatusTypeDef AZDM01_DigitalInputInitDefaultPA6(AZDM01_HandleTypeDef *hazdm)
{
	return AZDM01_DigitalInputInit(hazdm, AZDM01_DEFAULT_DIGITAL_PORT, AZDM01_DEFAULT_DIGITAL_PIN);
}

GPIO_PinState AZDM01_ReadDigital(AZDM01_HandleTypeDef *hazdm)
{
	if ((hazdm == NULL) || (hazdm->digitalPort == NULL) || (hazdm->digitalPin == 0u))
	{
		return GPIO_PIN_RESET;
	}

	return HAL_GPIO_ReadPin(hazdm->digitalPort, hazdm->digitalPin);
}

void AZDM01_SetCalibrationVoltage(AZDM01_HandleTypeDef *hazdm, float u0Voltage)
{
	if (hazdm == NULL)
	{
		return;
	}

	if (u0Voltage > 0.01f)
	{
		hazdm->calibVoltage = u0Voltage;
	}
}

void AZDM01_SetVref(AZDM01_HandleTypeDef *hazdm, float vrefVoltage)
{
	if ((hazdm == NULL) || (vrefVoltage <= 0.1f))
	{
		return;
	}
	hazdm->vref = vrefVoltage;
}

void AZDM01_SetTemperature(AZDM01_HandleTypeDef *hazdm, float tempC)
{
	if (hazdm == NULL)
	{
		return;
	}

	hazdm->tempC = AZDM01_ClampFloat(tempC, -20.0f, 80.0f);
}

void AZDM01_SetKValue(AZDM01_HandleTypeDef *hazdm, float kValue)
{
	if ((hazdm == NULL) || (kValue < 0.0f))
	{
		return;
	}

	hazdm->kValue = kValue;
}

void AZDM01_SetNtuMax(AZDM01_HandleTypeDef *hazdm, float ntuMax)
{
	if ((hazdm == NULL) || (ntuMax < 100.0f))
	{
		return;
	}

	hazdm->ntuMax = ntuMax;
}

HAL_StatusTypeDef AZDM01_SetPwmDuty(AZDM01_HandleTypeDef *hazdm, float dutyPercent)
{
	uint32_t arr;
	uint32_t ccr;

	if ((hazdm == NULL) || (hazdm->htimPwm == NULL))
	{
		return HAL_ERROR;
	}

	dutyPercent = AZDM01_ClampFloat(dutyPercent, 0.0f, 100.0f);

	arr = __HAL_TIM_GET_AUTORELOAD(hazdm->htimPwm);
	ccr = (uint32_t)(((arr + 1u) * dutyPercent) / 100.0f);
	if (ccr > arr)
	{
		ccr = arr;
	}

	__HAL_TIM_SET_COMPARE(hazdm->htimPwm, hazdm->pwmChannel, ccr);
	return HAL_OK;
}

HAL_StatusTypeDef AZDM01_ReadRaw(AZDM01_HandleTypeDef *hazdm, uint16_t *raw)
{
	return AZDM01_ReadRawAverage(hazdm, 1u, raw);
}

HAL_StatusTypeDef AZDM01_ReadVoltage(AZDM01_HandleTypeDef *hazdm, uint8_t samples, float *voltage)
{
	HAL_StatusTypeDef ret;
	uint16_t raw = 0u;

	if ((hazdm == NULL) || (voltage == NULL) || (hazdm->adcFullScale < 1.0f) || (hazdm->vref <= 0.1f))
	{
		return HAL_ERROR;
	}

	ret = AZDM01_ReadRawAverage(hazdm, samples, &raw);
	if (ret != HAL_OK)
	{
		return ret;
	}

	*voltage = ((float)raw * hazdm->vref) / hazdm->adcFullScale;
	return HAL_OK;
}

HAL_StatusTypeDef AZDM01_SaveCalibrationFromCurrent(AZDM01_HandleTypeDef *hazdm,
													float currentDutyPercent,
													uint8_t samples)
{
	HAL_StatusTypeDef ret;
	float v = 0.0f;

	if (hazdm == NULL)
	{
		return HAL_ERROR;
	}

	if (hazdm->htimPwm != NULL)
	{
		ret = AZDM01_SetPwmDuty(hazdm, currentDutyPercent);
		if (ret != HAL_OK)
		{
			return ret;
		}
	}

	ret = AZDM01_ReadVoltage(hazdm, samples, &v);
	if (ret != HAL_OK)
	{
		return ret;
	}

	hazdm->calibVoltage = v;
	hazdm->calibDutyPercent = AZDM01_ClampFloat(currentDutyPercent, 0.0f, 100.0f);
	return HAL_OK;
}

float AZDM01_GetFactor(AZDM01_HandleTypeDef *hazdm, float measuredVoltage)
{
	if ((hazdm == NULL) || (hazdm->calibVoltage <= 0.01f))
	{
		return 0.0f;
	}

	return measuredVoltage / hazdm->calibVoltage;
}

AZDM01_NTUGrade_t AZDM01_ClassifyNTU(float factorPercent)
{
	if (factorPercent >= 100.0f)
	{
		return AZDM01_NTU_CLEAR;
	}

	/* 兼容旧接口：仍按因子百分比进行大致分档。 */
	if (factorPercent >= 64.3f)
	{
		return AZDM01_NTU_500;
	}
	if (factorPercent >= 43.5f)
	{
		return AZDM01_NTU_1000;
	}
	if (factorPercent >= 23.6f)
	{
		return AZDM01_NTU_2000;
	}
	if (factorPercent >= 7.9f)
	{
		return AZDM01_NTU_4000;
	}

	return AZDM01_NTU_OVER_RANGE;
}

AZDM01_NTUGrade_t AZDM01_ClassifyNTUByValue(uint16_t ntuValue)
{
	if (ntuValue == 0u)
	{
		return AZDM01_NTU_CLEAR;
	}
	if (ntuValue <= 500u)
	{
		return AZDM01_NTU_500;
	}
	if (ntuValue <= 1000u)
	{
		return AZDM01_NTU_1000;
	}
	if (ntuValue <= 2000u)
	{
		return AZDM01_NTU_2000;
	}
	return AZDM01_NTU_4000;
}

uint16_t AZDM01_EstimateNTU(float factorPercent)
{
	/* 使用均值点进行分段线性估算：
	 * (100,0), (64.3,500), (43.5,1000), (23.6,2000), (7.9,4000)
	 */
	float x1, y1, x2, y2;
	float ntu;

	if (factorPercent >= 100.0f)
	{
		return 0u;
	}
	if (factorPercent >= 64.3f)
	{
		x1 = 100.0f;
		y1 = 0.0f;
		x2 = 64.3f;
		y2 = 500.0f;
	}
	else if (factorPercent >= 43.5f)
	{
		x1 = 64.3f;
		y1 = 500.0f;
		x2 = 43.5f;
		y2 = 1000.0f;
	}
	else if (factorPercent >= 23.6f)
	{
		x1 = 43.5f;
		y1 = 1000.0f;
		x2 = 23.6f;
		y2 = 2000.0f;
	}
	else if (factorPercent >= 7.9f)
	{
		x1 = 23.6f;
		y1 = 2000.0f;
		x2 = 7.9f;
		y2 = 4000.0f;
	}
	else
	{
		return 4000u;
	}

	ntu = y1 + ((factorPercent - x1) * (y2 - y1) / (x2 - x1));
	ntu = AZDM01_ClampFloat(ntu, 0.0f, 4000.0f);
	return (uint16_t)(ntu + 0.5f);
}

uint16_t AZDM01_EstimateNTUByVoltage(AZDM01_HandleTypeDef *hazdm, float measuredVoltage)
{
	if (measuredVoltage < 0.0f)
	{
		measuredVoltage = 0.0f;
	}

	return AZDM01_EstimateNTUByArduinoModel(hazdm, measuredVoltage);
}

HAL_StatusTypeDef AZDM01_Measure(AZDM01_HandleTypeDef *hazdm,
								 uint8_t samples,
								 AZDM01_ResultTypeDef *result)
{
	HAL_StatusTypeDef ret;
	uint16_t raw = 0u;
	float voltage;
	float factor;
	float factorPercent;

	if ((hazdm == NULL) || (result == NULL))
	{
		return HAL_ERROR;
	}

	ret = AZDM01_ReadRawAverage(hazdm, samples, &raw);
	if (ret != HAL_OK)
	{
		return ret;
	}

	voltage = ((float)raw * hazdm->vref) / hazdm->adcFullScale;
	factor = AZDM01_GetFactor(hazdm, voltage);
	factorPercent = factor * 100.0f;

	result->adcRaw = raw;
	result->voltage = voltage;
	result->factor = factor;
	result->factorPercent = factorPercent;
	result->ntuEstimate = AZDM01_EstimateNTUByArduinoModel(hazdm, voltage);
	result->ntuGrade = AZDM01_ClassifyNTUByValue(result->ntuEstimate);

	return HAL_OK;
}
