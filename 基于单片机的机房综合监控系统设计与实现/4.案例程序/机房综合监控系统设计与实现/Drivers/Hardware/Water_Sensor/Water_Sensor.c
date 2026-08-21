#include ".\Hardware\Water_Sensor\Water_Sensor.h"

static ADC_HandleTypeDef WaterSensor_ADC_Handle;

static void Water_Sensor_GPIO_ClockEnable(void)
{
	if (WATER_SENSOR_GPIO_PORT == GPIOA)
	{
		__HAL_RCC_GPIOA_CLK_ENABLE();
	}
	else if (WATER_SENSOR_GPIO_PORT == GPIOB)
	{
		__HAL_RCC_GPIOB_CLK_ENABLE();
	}
	else if (WATER_SENSOR_GPIO_PORT == GPIOC)
	{
		__HAL_RCC_GPIOC_CLK_ENABLE();
	}
	else if (WATER_SENSOR_GPIO_PORT == GPIOD)
	{
		__HAL_RCC_GPIOD_CLK_ENABLE();
	}
	else if (WATER_SENSOR_GPIO_PORT == GPIOE)
	{
		__HAL_RCC_GPIOE_CLK_ENABLE();
	}
}

void Water_Sensor_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	Water_Sensor_GPIO_ClockEnable();
	__HAL_RCC_ADC1_CLK_ENABLE();

	GPIO_InitStruct.Pin = WATER_SENSOR_AO_GPIO_PIN;
	GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	HAL_GPIO_Init(WATER_SENSOR_GPIO_PORT, &GPIO_InitStruct);

	WaterSensor_ADC_Handle.Instance = ADC1;
	WaterSensor_ADC_Handle.Init.ScanConvMode = ADC_SCAN_DISABLE;
	WaterSensor_ADC_Handle.Init.ContinuousConvMode = DISABLE;
	WaterSensor_ADC_Handle.Init.DiscontinuousConvMode = DISABLE;
	WaterSensor_ADC_Handle.Init.ExternalTrigConv = ADC_SOFTWARE_START;
	WaterSensor_ADC_Handle.Init.DataAlign = ADC_DATAALIGN_RIGHT;
	WaterSensor_ADC_Handle.Init.NbrOfConversion = 1;
	HAL_ADC_Init(&WaterSensor_ADC_Handle);

	HAL_ADCEx_Calibration_Start(&WaterSensor_ADC_Handle);
}

uint16_t Water_Sensor_GetAO_Raw(void)
{
	ADC_ChannelConfTypeDef sConfig = {0};

	sConfig.Channel = WATER_SENSOR_CHANNEL;
	sConfig.Rank = ADC_REGULAR_RANK_1;
	sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
	HAL_ADC_ConfigChannel(&WaterSensor_ADC_Handle, &sConfig);

	HAL_ADC_Start(&WaterSensor_ADC_Handle);
	HAL_ADC_PollForConversion(&WaterSensor_ADC_Handle, 10);

	return (uint16_t)HAL_ADC_GetValue(&WaterSensor_ADC_Handle);
}

float Water_Sensor_GetVoltage(void)
{
	uint16_t raw = Water_Sensor_GetAO_Raw();
	return ((float)raw / WATER_SENSOR_ADC_MAX) * WATER_SENSOR_ADC_VREF;
}

uint8_t Water_Sensor_GetPercent(void)
{
	uint16_t raw = Water_Sensor_GetAO_Raw();
	uint32_t percent = ((uint32_t)raw * 100u + 2047u) / 4095u;

	if (percent > 100u)
	{
		percent = 100u;
	}

	return (uint8_t)percent;
}

uint8_t Water_Sensor_IsWaterDetected(uint16_t threshold)
{
	return (Water_Sensor_GetAO_Raw() >= threshold) ? 1u : 0u;
}

