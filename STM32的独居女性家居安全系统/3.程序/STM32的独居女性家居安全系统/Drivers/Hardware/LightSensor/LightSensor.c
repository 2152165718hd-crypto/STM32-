#include ".\Hardware\LightSensor\LightSensor.h"

/* 静态 ADC 句柄，供模块内部使用 */
static ADC_HandleTypeDef hadc_light;
static uint8_t light_initialized = 0;

/* 读取光敏模块的数字信号（返回 0 或 1） */
uint8_t LightSensor_ReadDigital(void)
{
    GPIO_PinState state = HAL_GPIO_ReadPin(LIGHTSENSOR_Data_PORT, LIGHTSENSOR_Digital_PIN);
    return (state == GPIO_PIN_SET) ? 1 : 0;
}

/*
 * 读取光敏模块的模拟值（返回 ADC 原始值，12-bit MCU 返回 0..4095）
 * 无需传入 ADC 句柄，内部使用模块的 ADC 句柄。
 * 如果尚未初始化或出错，返回 0。
 */
uint16_t LightSensor_ReadAnalog(void)
{
    if (!light_initialized)
    {
        return 0;
    }

    ADC_HandleTypeDef *hadc = &hadc_light;

    if (HAL_ADC_Start(hadc) != HAL_OK)
    {
        return 0;
    }

    if (HAL_ADC_PollForConversion(hadc, 10) != HAL_OK)
    {
        HAL_ADC_Stop(hadc);
        return 0;
    }

    uint16_t val = (uint16_t)HAL_ADC_GetValue(hadc);
    HAL_ADC_Stop(hadc);
    return val;
}

/*
 * 单一初始化函数：配置 GPIO（数字输入 + 模拟），初始化 ADC 并配置通道。
 * 返回：1 表示成功，0 表示失败。
 * 示例：
 * if (LightSensor_Init()) { uint16_t v = LightSensor_ReadAnalog(); }
 */
uint8_t LightSensor_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* 使能 GPIOA 时钟（若其他地方已使能无害） */
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* 数字脚作为输入 */
    GPIO_InitStruct.Pin = LIGHTSENSOR_Digital_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(LIGHTSENSOR_Data_PORT, &GPIO_InitStruct);

    /* 模拟脚作为模拟输入（ADC） */
    GPIO_InitStruct.Pin = LIGHTSENSOR_Analog_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(LIGHTSENSOR_Data_PORT, &GPIO_InitStruct);

    /* ADC 初始化 */
    __HAL_RCC_ADC1_CLK_ENABLE();

    hadc_light.Instance = ADC1;
    hadc_light.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc_light.Init.ContinuousConvMode = DISABLE;
    hadc_light.Init.DiscontinuousConvMode = DISABLE;
    hadc_light.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc_light.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc_light.Init.NbrOfConversion = 1;

    if (HAL_ADC_Init(&hadc_light) != HAL_OK)
    {
        light_initialized = 0;
        return 0;
    }

#if defined(HAL_ADCEx_Calibration_Start)
    HAL_ADCEx_Calibration_Start(&hadc_light);
#endif

    /* 配置 ADC 通道 */
    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel = LIGHTSENSOR_ADC_CHANNEL;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_28CYCLES_5;
    if (HAL_ADC_ConfigChannel(&hadc_light, &sConfig) != HAL_OK)
    {
        light_initialized = 0;
        return 0;
    }

    light_initialized = 1;
    return 1;
}
