#include "./Hardware/MQ135_Smoke/MQ135_Smoke.h"

static ADC_HandleTypeDef MQ135_ADC_Handle;

void MQ135_Smoke_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_ADC1_CLK_ENABLE();

    /* DO PA0: pull-up input */
    GPIO_InitStruct.Pin = MQ135_SMOKE_DO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(MQ135_SMOKE_DO_PORT, &GPIO_InitStruct);

    /* AO PA1: direct ADC analog input */
    GPIO_InitStruct.Pin = MQ135_SMOKE_AO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(MQ135_SMOKE_AO_PORT, &GPIO_InitStruct);

    MQ135_ADC_Handle.Instance = ADC1;
    MQ135_ADC_Handle.Init.ScanConvMode = ADC_SCAN_DISABLE;
    MQ135_ADC_Handle.Init.ContinuousConvMode = DISABLE;
    MQ135_ADC_Handle.Init.DiscontinuousConvMode = DISABLE;
    MQ135_ADC_Handle.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    MQ135_ADC_Handle.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    MQ135_ADC_Handle.Init.NbrOfConversion = 1;
    HAL_ADC_Init(&MQ135_ADC_Handle);

    HAL_ADCEx_Calibration_Start(&MQ135_ADC_Handle);
}

uint8_t MQ135_Smoke_GetDO(void)
{
    return (uint8_t)HAL_GPIO_ReadPin(MQ135_SMOKE_DO_PORT, MQ135_SMOKE_DO_PIN);
}

uint16_t MQ135_Smoke_GetAO_Raw(void)
{
    ADC_ChannelConfTypeDef sConfig = {0};

    sConfig.Channel = MQ135_SMOKE_CHANNEL;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
    HAL_ADC_ConfigChannel(&MQ135_ADC_Handle, &sConfig);

    HAL_ADC_Start(&MQ135_ADC_Handle);
    HAL_ADC_PollForConversion(&MQ135_ADC_Handle, 10);

    return (uint16_t)HAL_ADC_GetValue(&MQ135_ADC_Handle);
}

float MQ135_Smoke_GetVoltage(void)
{
    uint16_t adc_raw = MQ135_Smoke_GetAO_Raw();
    float adc_voltage = (float)adc_raw / 4095.0f * MQ135_SMOKE_ADC_REF_VOLTAGE;

    return adc_voltage / MQ135_SMOKE_AO_DIVIDER_RATIO;
}
