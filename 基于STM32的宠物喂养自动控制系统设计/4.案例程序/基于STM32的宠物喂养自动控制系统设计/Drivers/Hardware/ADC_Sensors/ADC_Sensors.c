#include ".\Hardware\ADC_Sensors\ADC_Sensors.h"

/**
 * @file ADC_Sensors.c
 * @brief ADC 传感器模块实现（光照/水位/MQ135）。
 */

static ADC_HandleTypeDef hadc1_sensors;
static uint8_t adc_sensors_inited = 0U;

/**
 * @brief 根据端口使能对应 GPIO 时钟。
 */
static void ADC_Sensors_GPIO_Clock_Enable(GPIO_TypeDef *port)
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
#if defined(GPIOD)
	else if (port == GPIOD)
	{
		__HAL_RCC_GPIOD_CLK_ENABLE();
	}
#endif
#if defined(GPIOE)
	else if (port == GPIOE)
	{
		__HAL_RCC_GPIOE_CLK_ENABLE();
	}
#endif
}

/**
 * @brief 将 ADC 原始值换算为电压值（单位 V）。
 */
static float ADC_Sensors_RawToVoltage(uint16_t raw)
{
	return ((float)raw * (float)ADC_SENSORS_VREF_MV) / ((float)ADC_SENSORS_MAX_VALUE * 1000.0f);
}

void ADC_Sensors_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	__HAL_RCC_ADC1_CLK_ENABLE();

	ADC_Sensors_GPIO_Clock_Enable(LightSensor_ADC_PORT);
	ADC_Sensors_GPIO_Clock_Enable(Water_ADC_PORT);
	ADC_Sensors_GPIO_Clock_Enable(MQ135_ADC_PORT);
	ADC_Sensors_GPIO_Clock_Enable(MQ135_DO_PORT);

	/* ADC 模拟输入引脚配置 */
	GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
	GPIO_InitStruct.Pull = GPIO_NOPULL;

	GPIO_InitStruct.Pin = LightSensor_ADC_PIN;
	HAL_GPIO_Init(LightSensor_ADC_PORT, &GPIO_InitStruct);

	GPIO_InitStruct.Pin = Water_ADC_PIN;
	HAL_GPIO_Init(Water_ADC_PORT, &GPIO_InitStruct);

	GPIO_InitStruct.Pin = MQ135_ADC_PIN;
	HAL_GPIO_Init(MQ135_ADC_PORT, &GPIO_InitStruct);

	/* MQ135 DO 数字口配置 */
	GPIO_InitStruct.Pin = MQ135_DO_PIN;
	GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	GPIO_InitStruct.Pull = GPIO_PULLUP;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(MQ135_DO_PORT, &GPIO_InitStruct);

	hadc1_sensors.Instance = ADC1;
	hadc1_sensors.Init.ScanConvMode = ADC_SCAN_DISABLE;
	hadc1_sensors.Init.ContinuousConvMode = DISABLE;
	hadc1_sensors.Init.DiscontinuousConvMode = DISABLE;
	hadc1_sensors.Init.NbrOfDiscConversion = 0U;
	hadc1_sensors.Init.ExternalTrigConv = ADC_SOFTWARE_START;
	hadc1_sensors.Init.DataAlign = ADC_DATAALIGN_RIGHT;
	hadc1_sensors.Init.NbrOfConversion = 1;

	if (HAL_ADC_Init(&hadc1_sensors) != HAL_OK)
	{
		adc_sensors_inited = 0U;
		return;
	}

	if (HAL_ADCEx_Calibration_Start(&hadc1_sensors) != HAL_OK)
	{
		adc_sensors_inited = 0U;
		return;
	}

	adc_sensors_inited = 1U;
}

uint16_t ADC_Sensors_ReadChannelRaw(uint32_t channel)
{
	ADC_ChannelConfTypeDef sConfig = {0};
	uint32_t sum = 0U;
	uint8_t i;

	if (!adc_sensors_inited)
	{
		ADC_Sensors_Init();
		if (!adc_sensors_inited)
		{
			return 0U;
		}
	}

	sConfig.Channel = channel;
	sConfig.Rank = ADC_REGULAR_RANK_1;
	sConfig.SamplingTime = ADC_SENSORS_SAMPLE_TIME;

	if (HAL_ADC_ConfigChannel(&hadc1_sensors, &sConfig) != HAL_OK)
	{
		return 0U;
	}

	for (i = 0U; i < ADC_SENSORS_DEFAULT_SAMPLES; i++)
	{
		if (HAL_ADC_Start(&hadc1_sensors) != HAL_OK)
		{
			HAL_ADC_Stop(&hadc1_sensors);
			return 0U;
		}

		if (HAL_ADC_PollForConversion(&hadc1_sensors, ADC_SENSORS_TIMEOUT_MS) != HAL_OK)
		{
			HAL_ADC_Stop(&hadc1_sensors);
			return 0U;
		}

		sum += HAL_ADC_GetValue(&hadc1_sensors);
		HAL_ADC_Stop(&hadc1_sensors);
	}

	return (uint16_t)(sum / ADC_SENSORS_DEFAULT_SAMPLES);
}

float ADC_Sensors_ReadChannelVoltage(uint32_t channel)
{
	return ADC_Sensors_RawToVoltage(ADC_Sensors_ReadChannelRaw(channel));
}

uint16_t LightSensor_ReadRaw(void)
{
	return ADC_Sensors_ReadChannelRaw(LightSensor_ADC_CHANNEL);
}

float LightSensor_ReadVoltage(void)
{
	return ADC_Sensors_ReadChannelVoltage(LightSensor_ADC_CHANNEL);
}

uint16_t WaterSensor_ReadRaw(void)
{
	return ADC_Sensors_ReadChannelRaw(Water_ADC_CHANNEL);
}

float WaterSensor_ReadVoltage(void)
{
	return ADC_Sensors_ReadChannelVoltage(Water_ADC_CHANNEL);
}

uint16_t MQ135_ReadRaw(void)
{
	return ADC_Sensors_ReadChannelRaw(MQ135_ADC_CHANNEL);
}

float MQ135_ReadVoltage(void)
{
	return ADC_Sensors_ReadChannelVoltage(MQ135_ADC_CHANNEL);
}

float MQ135_ReadOutputVoltage(void)
{
	return MQ135_ReadVoltage() * (MQ135_VOLTAGE_RATIO_NUM / MQ135_VOLTAGE_RATIO_DEN);
}

uint8_t MQ135_ReadDO(void)
{
	return (uint8_t)HAL_GPIO_ReadPin(MQ135_DO_PORT, MQ135_DO_PIN);
}



