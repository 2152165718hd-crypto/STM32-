#include "Hardware/LightSensor/LightSensor.h"

#define LIGHT_AO_GPIO_PORT GPIOA
#define LIGHT_AO_GPIO_PIN GPIO_PIN_2
#define LIGHT_DO_GPIO_PORT GPIOA
#define LIGHT_DO_GPIO_PIN GPIO_PIN_1
#define LIGHT_ADC_CHANNEL ADC_CHANNEL_2
#define LIGHT_ADC_MAX 4095u
#define LIGHT_SAMPLE_COUNT 4u

static ADC_HandleTypeDef g_light_adc;
static uint8_t g_light_ready = 0u;

HAL_StatusTypeDef Light_Init(void)
{
    GPIO_InitTypeDef gpio_init = {0};
    ADC_ChannelConfTypeDef adc_channel = {0};
    RCC_PeriphCLKInitTypeDef adc_clock = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_ADC1_CLK_ENABLE();

    gpio_init.Pin = LIGHT_AO_GPIO_PIN;
    gpio_init.Mode = GPIO_MODE_ANALOG;
    HAL_GPIO_Init(LIGHT_AO_GPIO_PORT, &gpio_init);

    gpio_init.Pin = LIGHT_DO_GPIO_PIN;
    gpio_init.Mode = GPIO_MODE_INPUT;
    gpio_init.Pull = GPIO_PULLUP;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LIGHT_DO_GPIO_PORT, &gpio_init);

    adc_clock.PeriphClockSelection = RCC_PERIPHCLK_ADC;
    adc_clock.AdcClockSelection = RCC_ADCPCLK2_DIV6;
    HAL_RCCEx_PeriphCLKConfig(&adc_clock);

    g_light_adc.Instance = ADC1;
    g_light_adc.Init.ScanConvMode = ADC_SCAN_DISABLE;
    g_light_adc.Init.ContinuousConvMode = DISABLE;
    g_light_adc.Init.DiscontinuousConvMode = DISABLE;
    g_light_adc.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    g_light_adc.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    g_light_adc.Init.NbrOfConversion = 1u;

    if (HAL_ADC_Init(&g_light_adc) != HAL_OK)
    {
        g_light_ready = 0u;
        return HAL_ERROR;
    }

    adc_channel.Channel = LIGHT_ADC_CHANNEL;
    adc_channel.Rank = ADC_REGULAR_RANK_1;
    adc_channel.SamplingTime = ADC_SAMPLETIME_71CYCLES_5;

    if (HAL_ADC_ConfigChannel(&g_light_adc, &adc_channel) != HAL_OK)
    {
        g_light_ready = 0u;
        return HAL_ERROR;
    }

    HAL_ADCEx_Calibration_Start(&g_light_adc);
    g_light_ready = 1u;
    return HAL_OK;
}

uint16_t Light_ReadRaw(void)
{
    uint32_t i;
    uint32_t sum = 0u;

    if (g_light_ready == 0u)
    {
        return 0u;
    }

    for (i = 0u; i < LIGHT_SAMPLE_COUNT; i++)
    {
        HAL_ADC_Start(&g_light_adc);
        if (HAL_ADC_PollForConversion(&g_light_adc, 10u) == HAL_OK)
        {
            sum += HAL_ADC_GetValue(&g_light_adc);
        }
        HAL_ADC_Stop(&g_light_adc);
    }

    return (uint16_t)(sum / LIGHT_SAMPLE_COUNT);
}

uint8_t Light_ReadPercent(void)
{
    return Light_ConvertRawToPercent(Light_ReadRaw());
}

uint8_t Light_ConvertRawToPercent(uint16_t raw)
{
    uint32_t scaled = raw;

    if (scaled > LIGHT_ADC_MAX)
    {
        scaled = LIGHT_ADC_MAX;
    }

#if (LIGHT_ANALOG_INVERT != 0u)
    scaled = LIGHT_ADC_MAX - scaled;
#endif

    return (uint8_t)((scaled * 100u + (LIGHT_ADC_MAX / 2u)) / LIGHT_ADC_MAX);
}

GPIO_PinState Light_ReadDigital(void)
{
    return HAL_GPIO_ReadPin(LIGHT_DO_GPIO_PORT, LIGHT_DO_GPIO_PIN);
}
