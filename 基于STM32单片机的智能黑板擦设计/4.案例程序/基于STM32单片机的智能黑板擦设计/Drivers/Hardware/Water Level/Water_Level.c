#include ".\Hardware\Water Level\Water_Level.h"

static ADC_HandleTypeDef h_water_adc;

static void WaterLevel_GPIO_Init(void)
{
	__HAL_RCC_GPIOA_CLK_ENABLE();

	GPIO_InitTypeDef gpio = {0};
	gpio.Pin = WATER_LEVEL_PIN;
	gpio.Mode = GPIO_MODE_ANALOG;
	HAL_GPIO_Init(WATER_LEVEL_PORT, &gpio);
}

static void WaterLevel_ADC_Init(void)
{
	ADC_ChannelConfTypeDef sConfig = {0};

	__HAL_RCC_ADC1_CLK_ENABLE();

	h_water_adc.Instance = ADC1;
	h_water_adc.Init.ScanConvMode = ADC_SCAN_DISABLE;
	h_water_adc.Init.ContinuousConvMode = DISABLE;
	h_water_adc.Init.DiscontinuousConvMode = DISABLE;
	h_water_adc.Init.ExternalTrigConv = ADC_SOFTWARE_START;
	h_water_adc.Init.DataAlign = ADC_DATAALIGN_RIGHT;
	h_water_adc.Init.NbrOfConversion = 1;
	HAL_ADC_Init(&h_water_adc);

	sConfig.Channel = ADC_CHANNEL_6;
	sConfig.Rank = ADC_REGULAR_RANK_1;
	sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
	HAL_ADC_ConfigChannel(&h_water_adc, &sConfig);

	HAL_ADCEx_Calibration_Start(&h_water_adc);
}

void WaterLevel_Init(void)
{
	WaterLevel_GPIO_Init();
	WaterLevel_ADC_Init();
}

uint16_t WaterLevel_ReadRaw(void)
{
	uint16_t value = 0u;

	if (HAL_ADC_Start(&h_water_adc) == HAL_OK)
	{
		if (HAL_ADC_PollForConversion(&h_water_adc, 10u) == HAL_OK)
		{
			value = (uint16_t)HAL_ADC_GetValue(&h_water_adc);
		}
		HAL_ADC_Stop(&h_water_adc);
	}

	return value;
}

uint16_t WaterLevel_ReadRawAvg(uint8_t samples)
{
	uint32_t sum = 0u;
	uint8_t i;

	if (samples == 0u)
	{
		samples = 1u;
	}

	for (i = 0u; i < samples; i++)
	{
		sum += WaterLevel_ReadRaw();
	}

	return (uint16_t)(sum / samples);
}

uint8_t WaterLevel_ReadPercent(void)
{
	uint32_t raw = WaterLevel_ReadRawAvg(8u);

	if (raw <= WATER_LEVEL_ADC_MIN)
	{
		return 0u;
	}

	if (raw >= WATER_LEVEL_ADC_MAX)
	{
		return 100u;
	}

	return (uint8_t)(((raw - WATER_LEVEL_ADC_MIN) * 100u) / (WATER_LEVEL_ADC_MAX - WATER_LEVEL_ADC_MIN));
}

uint8_t WaterLevel_IsAlarm(uint8_t threshold_percent)
{
	if (threshold_percent > 100u)
	{
		threshold_percent = 100u;
	}

	return (WaterLevel_ReadPercent() >= threshold_percent) ? 1u : 0u;
}
