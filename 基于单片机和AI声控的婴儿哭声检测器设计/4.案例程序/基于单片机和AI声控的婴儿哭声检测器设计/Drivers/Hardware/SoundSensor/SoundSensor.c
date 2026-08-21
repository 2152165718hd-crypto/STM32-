#include ".\Hardware\SoundSensor\SoundSensor.h"

static ADC_HandleTypeDef hadc_sound;

void SoundSensor_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    ADC_ChannelConfTypeDef sConfig = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* STM32F1 ADC 最大时钟建议不超过 14MHz，72MHz 主频下将 ADC 时钟分频为 6 */
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
    PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
    HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit);

    __HAL_RCC_ADC1_CLK_ENABLE();

    /* AO 对应 ADC 输入，需配置为模拟模式 */
    GPIO_InitStruct.Pin = SOUND_SENSOR_AO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    HAL_GPIO_Init(SOUND_SENSOR_GPIO_PORT, &GPIO_InitStruct);

    /* DO 为比较器数字输出，默认上拉输入 */
    GPIO_InitStruct.Pin = SOUND_SENSOR_DO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(SOUND_SENSOR_GPIO_PORT, &GPIO_InitStruct);

    hadc_sound.Instance = ADC1;
    hadc_sound.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc_sound.Init.ContinuousConvMode = DISABLE;
    hadc_sound.Init.DiscontinuousConvMode = DISABLE;
    hadc_sound.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc_sound.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc_sound.Init.NbrOfConversion = 1;
    HAL_ADC_Init(&hadc_sound);

    sConfig.Channel = SOUND_SENSOR_ADC_CHANNEL;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
    HAL_ADC_ConfigChannel(&hadc_sound, &sConfig);

    HAL_ADCEx_Calibration_Start(&hadc_sound);
}

uint16_t SoundSensor_ReadAnalog(void)
{
    uint16_t value = 0u;

    if (HAL_ADC_Start(&hadc_sound) != HAL_OK)
    {
        return 0u;
    }

    if (HAL_ADC_PollForConversion(&hadc_sound, 10u) == HAL_OK)
    {
        value = (uint16_t)HAL_ADC_GetValue(&hadc_sound);
    }

    HAL_ADC_Stop(&hadc_sound);
    return value;
}

uint16_t SoundSensor_ReadAnalogAverage(uint8_t sampleCount, uint16_t sampleIntervalMs)
{
    uint32_t sum = 0u;
    uint8_t i;

    if (sampleCount == 0u)
    {
        return 0u;
    }

    for (i = 0u; i < sampleCount; i++)
    {
        sum += SoundSensor_ReadAnalog();

        if ((sampleIntervalMs > 0u) && (i < (sampleCount - 1u)))
        {
            delay_ms(sampleIntervalMs);
        }
    }

    return (uint16_t)(sum / sampleCount);
}

uint8_t SoundSensor_ReadDigital(void)
{
    return (HAL_GPIO_ReadPin(SOUND_SENSOR_GPIO_PORT, SOUND_SENSOR_DO_PIN) == GPIO_PIN_SET) ? 1u : 0u;
}

uint8_t SoundSensor_IsTriggered(void)
{
    return (HAL_GPIO_ReadPin(SOUND_SENSOR_GPIO_PORT, SOUND_SENSOR_DO_PIN) == SOUND_SENSOR_DO_TRIGGER_LEVEL) ? 1u : 0u;
}
