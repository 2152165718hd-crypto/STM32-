#include ".\Hardware\DECIBEL\DECIBEL.h"

static ADC_HandleTypeDef s_hadc1;
static uint8_t s_decibelInited = 0u;

void Decibel_Init(void)
{
	GPIO_InitTypeDef gpioInit = {0};
	ADC_ChannelConfTypeDef adcChannelConf = {0};

	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_ADC1_CLK_ENABLE();

	gpioInit.Pin = DECIBEL_GPIO_PIN;
	gpioInit.Mode = GPIO_MODE_ANALOG;
	HAL_GPIO_Init(DECIBEL_GPIO_PORT, &gpioInit);

	s_hadc1.Instance = DECIBEL_ADC_INSTANCE;
	s_hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
	s_hadc1.Init.ContinuousConvMode = DISABLE;
	s_hadc1.Init.DiscontinuousConvMode = DISABLE;
	s_hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
	s_hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
	s_hadc1.Init.NbrOfConversion = 1;

	if (HAL_ADC_Init(&s_hadc1) != HAL_OK)
	{
		while (1)
		{
		}
	}

	adcChannelConf.Channel = DECIBEL_ADC_CHANNEL;
	adcChannelConf.Rank = ADC_REGULAR_RANK_1;
	adcChannelConf.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
	if (HAL_ADC_ConfigChannel(&s_hadc1, &adcChannelConf) != HAL_OK)
	{
		while (1)
		{
		}
	}

	if (HAL_ADCEx_Calibration_Start(&s_hadc1) != HAL_OK)
	{
		while (1)
		{
		}
	}

	s_decibelInited = 1u;
}

uint16_t Decibel_ReadRaw(void)
{
	uint32_t value = 0u;

	if (s_decibelInited == 0u)
	{
		Decibel_Init();
	}

	if (HAL_ADC_Start(&s_hadc1) != HAL_OK)
	{
		return 0u;
	}

	if (HAL_ADC_PollForConversion(&s_hadc1, 10u) == HAL_OK)
	{
		value = HAL_ADC_GetValue(&s_hadc1);
	}

	(void)HAL_ADC_Stop(&s_hadc1);

	if (value > DECIBEL_ADC_MAX)
	{
		value = DECIBEL_ADC_MAX;
	}

	return (uint16_t)value;
}

uint16_t Decibel_ReadRawAverage(uint8_t samples)
{
	uint32_t sum = 0u;
	uint8_t i;

	if (samples == 0u)
	{
		samples = 1u;
	}

	for (i = 0u; i < samples; i++)
	{
		sum += Decibel_ReadRaw();
	}

	return (uint16_t)(sum / samples);
}

uint32_t Decibel_RawToMilliVolt(uint16_t raw)
{
	if (raw > DECIBEL_ADC_MAX)
	{
		raw = DECIBEL_ADC_MAX;
	}

	return ((uint32_t)raw * DECIBEL_VREF_MV) / DECIBEL_ADC_MAX;
}

uint32_t Decibel_ReadMilliVolt(void)
{
	return Decibel_RawToMilliVolt(Decibel_ReadRawAverage(DECIBEL_DEFAULT_SAMPLES));
}

uint8_t Decibel_GetPercent(uint16_t raw)
{
	uint32_t mv = Decibel_RawToMilliVolt(raw);
	uint32_t percent;

	if (mv >= DECIBEL_OUTPUT_MAX_MV)
	{
		return 100u;
	}

	percent = (mv * 100u) / DECIBEL_OUTPUT_MAX_MV;
	if (percent > 100u)
	{
		percent = 100u;
	}

	return (uint8_t)percent;
}

uint8_t Decibel_ReadPercent(void)
{
	return Decibel_GetPercent(Decibel_ReadRawAverage(DECIBEL_DEFAULT_SAMPLES));
}
