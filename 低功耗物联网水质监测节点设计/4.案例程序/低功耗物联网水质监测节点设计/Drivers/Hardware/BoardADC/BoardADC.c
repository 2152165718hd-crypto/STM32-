#include ".\Hardware\BoardADC\BoardADC.h"
#include "stm32f1xx_hal_adc_ex.h"

static ADC_HandleTypeDef s_hadc1;
static uint8_t s_adc_ready = 0U;

uint8_t BoardADC_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_ADC1_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_2 | GPIO_PIN_4;
    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);

    s_hadc1.Instance = ADC1;
    s_hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    s_hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
    s_hadc1.Init.ContinuousConvMode = DISABLE;
    s_hadc1.Init.NbrOfConversion = 1U;
    s_hadc1.Init.DiscontinuousConvMode = DISABLE;
    s_hadc1.Init.NbrOfDiscConversion = 1U;
    s_hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;

    if (HAL_ADC_Init(&s_hadc1) != HAL_OK)
    {
        s_adc_ready = 0U;
        return 0U;
    }

    (void)HAL_ADCEx_Calibration_Start(&s_hadc1);
    s_adc_ready = 1U;
    return 1U;
}

uint16_t BoardADC_ReadRaw(BoardADC_Channel_t channel)
{
    ADC_ChannelConfTypeDef cfg = {0};
    uint32_t value = 0U;

    if (s_adc_ready == 0U)
    {
        (void)BoardADC_Init();
    }

    cfg.Channel = (uint32_t)channel;
    cfg.Rank = ADC_REGULAR_RANK_1;
    cfg.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;

    if (HAL_ADC_ConfigChannel(&s_hadc1, &cfg) != HAL_OK)
    {
        return 0U;
    }

    if (HAL_ADC_Start(&s_hadc1) != HAL_OK)
    {
        return 0U;
    }

    if (HAL_ADC_PollForConversion(&s_hadc1, 10U) == HAL_OK)
    {
        value = HAL_ADC_GetValue(&s_hadc1);
    }

    (void)HAL_ADC_Stop(&s_hadc1);
    return (uint16_t)(value & 0x0FFFU);
}

uint16_t BoardADC_ReadAverageRaw(BoardADC_Channel_t channel, uint8_t samples)
{
    uint32_t sum = 0U;
    uint8_t count = samples;

    if (count == 0U)
    {
        count = 1U;
    }

    for (uint8_t i = 0U; i < count; i++)
    {
        sum += BoardADC_ReadRaw(channel);
    }

    return (uint16_t)(sum / count);
}

float BoardADC_RawToVoltage(uint16_t raw)
{
    return ((float)raw * (float)BOARD_ADC_VREF_MV) / (4095.0f * 1000.0f);
}

float BoardADC_ReadVoltage(BoardADC_Channel_t channel)
{
    return BoardADC_RawToVoltage(BoardADC_ReadAverageRaw(channel, 8U));
}
