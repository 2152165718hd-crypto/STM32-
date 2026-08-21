#include ".\Hardware\Sensors\Sensors.h"

static ADC_HandleTypeDef sensors_hadc1;
static uint8_t sensors_adc_initialized = 0;

static void Sensors_ADC_InitOnce(void)
{
    if (sensors_adc_initialized)
    {
        return;
    }

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_ADC1_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = SOUND_SENSOR_GPIO_PIN | RAIN_SENSOR_GPIO_PIN | LIGHT_SENSOR_GPIO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    sensors_hadc1.Instance = ADC1;
    sensors_hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
    sensors_hadc1.Init.ContinuousConvMode = DISABLE;
    sensors_hadc1.Init.DiscontinuousConvMode = DISABLE;
    sensors_hadc1.Init.NbrOfDiscConversion = 0;
    sensors_hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    sensors_hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    sensors_hadc1.Init.NbrOfConversion = 1;
    if (HAL_ADC_Init(&sensors_hadc1) != HAL_OK)
    {
        return;
    }

    HAL_ADCEx_Calibration_Start(&sensors_hadc1);
    sensors_adc_initialized = 1;
}

static uint16_t Sensors_ReadChannel(uint32_t channel)
{
    ADC_ChannelConfTypeDef sConfig = {0};

    Sensors_ADC_InitOnce();
    if (!sensors_adc_initialized)
    {
        return 0;
    }

    sConfig.Channel = channel;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
    if (HAL_ADC_ConfigChannel(&sensors_hadc1, &sConfig) != HAL_OK)
    {
        return 0;
    }

    if (HAL_ADC_Start(&sensors_hadc1) != HAL_OK)
    {
        return 0;
    }

    if (HAL_ADC_PollForConversion(&sensors_hadc1, 10) != HAL_OK)
    {
        HAL_ADC_Stop(&sensors_hadc1);
        return 0;
    }

    uint16_t value = (uint16_t)HAL_ADC_GetValue(&sensors_hadc1);
    HAL_ADC_Stop(&sensors_hadc1);
    return value;
}

void LightSensor_Init(void)
{
    Sensors_ADC_InitOnce();
}

void SoundSensor_Init(void)
{
    Sensors_ADC_InitOnce();
}

void RainSensor_Init(void)
{
    Sensors_ADC_InitOnce();
}

uint16_t LightSensor_ReadAnalog(void)
{
    return Sensors_ReadChannel(LIGHT_SENSOR_ADC_CHANNEL);
}

uint16_t SoundSensor_ReadAnalog(void)
{
    return Sensors_ReadChannel(SOUND_SENSOR_ADC_CHANNEL);
}

uint16_t RainSensor_ReadAnalog(void)
{
    return Sensors_ReadChannel(RAIN_SENSOR_ADC_CHANNEL);
}


