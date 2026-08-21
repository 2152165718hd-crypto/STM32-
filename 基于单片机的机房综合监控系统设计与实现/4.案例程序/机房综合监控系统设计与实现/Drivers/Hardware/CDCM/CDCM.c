#include ".\Hardware\CDCM\CDCM.h"

static ADC_HandleTypeDef CDCM_ADC_Handle;

static void CDCM_GPIO_ClockEnable(GPIO_TypeDef *port)
{
	if (port == GPIOA)
	{
		__HAL_RCC_GPIOA_CLK_ENABLE();
	}
	else if (port == GPIOB)
	{
		__HAL_RCC_GPIOB_CLK_ENABLE();
	}
	else if (port == GPIOC)
	{
		__HAL_RCC_GPIOC_CLK_ENABLE();
	}
	else if (port == GPIOD)
	{
		__HAL_RCC_GPIOD_CLK_ENABLE();
	}
	else if (port == GPIOE)
	{
		__HAL_RCC_GPIOE_CLK_ENABLE();
	}
}

void CDCM_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	CDCM_GPIO_ClockEnable(CDCM_AO_GPIO_PORT);
	CDCM_GPIO_ClockEnable(CDCM_DO_GPIO_PORT);
	__HAL_RCC_ADC1_CLK_ENABLE();

	/* AO: 模拟输入 */
	GPIO_InitStruct.Pin = CDCM_AO_GPIO_PIN;
	GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	HAL_GPIO_Init(CDCM_AO_GPIO_PORT, &GPIO_InitStruct);

	/* DO: 数字输入（带上拉） */
	GPIO_InitStruct.Pin = CDCM_DO_GPIO_PIN;
	GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	GPIO_InitStruct.Pull = GPIO_PULLUP;
	HAL_GPIO_Init(CDCM_DO_GPIO_PORT, &GPIO_InitStruct);

	CDCM_ADC_Handle.Instance = ADC1;
	CDCM_ADC_Handle.Init.ScanConvMode = ADC_SCAN_DISABLE;
	CDCM_ADC_Handle.Init.ContinuousConvMode = DISABLE;
	CDCM_ADC_Handle.Init.DiscontinuousConvMode = DISABLE;
	CDCM_ADC_Handle.Init.ExternalTrigConv = ADC_SOFTWARE_START;
	CDCM_ADC_Handle.Init.DataAlign = ADC_DATAALIGN_RIGHT;
	CDCM_ADC_Handle.Init.NbrOfConversion = 1;
	HAL_ADC_Init(&CDCM_ADC_Handle);

	HAL_ADCEx_Calibration_Start(&CDCM_ADC_Handle);
}

uint8_t CDCM_GetDO(void)
{
	return (uint8_t)HAL_GPIO_ReadPin(CDCM_DO_GPIO_PORT, CDCM_DO_GPIO_PIN);
}

uint16_t CDCM_GetAO_Raw(void)
{
	ADC_ChannelConfTypeDef sConfig = {0};

	sConfig.Channel = CDCM_ADC_CHANNEL;
	sConfig.Rank = ADC_REGULAR_RANK_1;
	sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
	HAL_ADC_ConfigChannel(&CDCM_ADC_Handle, &sConfig);

	HAL_ADC_Start(&CDCM_ADC_Handle);
	HAL_ADC_PollForConversion(&CDCM_ADC_Handle, 10);

	return (uint16_t)HAL_ADC_GetValue(&CDCM_ADC_Handle);
}

float CDCM_GetADCVoltage(void)
{
	uint16_t adcRaw = CDCM_GetAO_Raw();
	return ((float)adcRaw / CDCM_ADC_MAX) * CDCM_ADC_VREF;
}

float CDCM_GetSensorVoltage(void)
{
	float adcVoltage = CDCM_GetADCVoltage();
	return adcVoltage / CDCM_AO_DIVIDER_RATIO;
}

float CDCM_GetCurrentA(void)
{
	float sensorVoltage = CDCM_GetSensorVoltage();
	return (sensorVoltage - CDCM_ZERO_CURRENT_VOLTAGE) / CDCM_SENSITIVITY_V_PER_A;
}

float CDCM_GetCurrentAbsA(void)
{
	float current = CDCM_GetCurrentA();
	return (current >= 0.0f) ? current : -current;
}

