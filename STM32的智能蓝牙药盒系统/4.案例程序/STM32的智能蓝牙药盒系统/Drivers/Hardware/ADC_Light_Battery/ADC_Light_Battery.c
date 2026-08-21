#include ".\Hardware\ADC_Light_Battery\ADC_Light_Battery.h"

/**
 * @file ADC_Light_Battery.c
 * @brief 光照与电池电压采样驱动实现。
 */

/* 静态 ADC 句柄，光照和电池共用 ADC1 */
static ADC_HandleTypeDef hadc1;
static uint8_t adc_initialized = 0;

/* 滤波状态 */
static float battery_voltage_filtered = 0.0f; /* 低通滤波后的电压 */
static uint8_t battery_percent_last = 0;      /* 上一次输出的电量 */
static uint8_t battery_filter_ready = 0;      /* 首次采样标记 */

/* 内部函数：切换 ADC 通道并读取一次原始值 */
static uint16_t ADC_ReadChannel(uint32_t channel)
{
    if (!adc_initialized)
        return 0;

    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel = channel;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
        return 0;

    if (HAL_ADC_Start(&hadc1) != HAL_OK)
        return 0;

    if (HAL_ADC_PollForConversion(&hadc1, 10) != HAL_OK)
    {
        HAL_ADC_Stop(&hadc1);
        return 0;
    }

    uint16_t val = (uint16_t)HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
    return val;
}

/* 读取光照传感器模拟值（0~4095） */
uint16_t LightSensor_ReadAnalog(void)
{
    return ADC_ReadChannel(LIGHTSENSOR_ADC_CHANNEL);
}

/* 读取电池 ADC 原始值（0~4095），单次无滤波 */
uint16_t Battery_ReadAnalog(void)
{
    return ADC_ReadChannel(BATTERY_ADC_CHANNEL);
}

/*
 * 内部：多次采样 + 去最大最小值 + 取平均（中值平均滤波）
 * 有效抑制毛刺和瞬态干扰
 */
static uint16_t Battery_ReadFiltered(void)
{
    uint16_t buf[BATTERY_SAMPLE_COUNT];
    uint16_t i, j, temp;

    /* 采集 BATTERY_SAMPLE_COUNT 次 */
    for (i = 0; i < BATTERY_SAMPLE_COUNT; i++)
        buf[i] = ADC_ReadChannel(BATTERY_ADC_CHANNEL);

    /* 冒泡排序 */
    for (i = 0; i < BATTERY_SAMPLE_COUNT - 1; i++)
        for (j = 0; j < BATTERY_SAMPLE_COUNT - 1 - i; j++)
            if (buf[j] > buf[j + 1])
            {
                temp = buf[j];
                buf[j] = buf[j + 1];
                buf[j + 1] = temp;
            }

    /* 去掉最大两个和最小两个，对中间值取平均 */
    uint32_t sum = 0;
    for (i = 2; i < BATTERY_SAMPLE_COUNT - 2; i++)
        sum += buf[i];

    return (uint16_t)(sum / (BATTERY_SAMPLE_COUNT - 4));
}

/*
 * 获取电池实际电压（V），经一阶低通滤波平滑
 * ADC 采样到的是电池电压的 27/37，反算需乘以 37/27
 */
float Battery_GetVoltage(void)
{
    uint16_t adc_val = Battery_ReadFiltered();
    float voltage = (float)adc_val / BATTERY_ADC_MAX * BATTERY_VREF * BATTERY_DIVIDER_RATIO;

    if (!battery_filter_ready)
    {
        /* 首次直接赋值，避免从0慢慢爬升 */
        battery_voltage_filtered = voltage;
        battery_filter_ready = 1;
    }
    else
    {
        /* 一阶低通滤波：y = α·x + (1-α)·y_prev */
        battery_voltage_filtered = BATTERY_FILTER_ALPHA * voltage +
                                   (1.0f - BATTERY_FILTER_ALPHA) * battery_voltage_filtered;
    }

    return battery_voltage_filtered;
}

/*
 * 获取电量百分比（0~100）
 * 在线性映射基础上增加限幅：每次调用变化不超过 BATTERY_PERCENT_MAX_STEP
 * 防止显示端电量跳变
 */
uint8_t Battery_GetPercent(void)
{
    float voltage = Battery_GetVoltage();
    uint8_t percent;

    if (voltage >= BATTERY_FULL_VOLTAGE)
        percent = 100;
    else if (voltage <= BATTERY_EMPTY_VOLTAGE)
        percent = 0;
    else
        percent = (uint8_t)((voltage - BATTERY_EMPTY_VOLTAGE) /
                            (BATTERY_FULL_VOLTAGE - BATTERY_EMPTY_VOLTAGE) * 100.0f);

    /* 限幅：单次变化不超过 BATTERY_PERCENT_MAX_STEP */
    if (battery_filter_ready > 1)
    {
        if (percent > battery_percent_last + BATTERY_PERCENT_MAX_STEP)
            percent = battery_percent_last + BATTERY_PERCENT_MAX_STEP;
        else if (percent + BATTERY_PERCENT_MAX_STEP < battery_percent_last)
            percent = battery_percent_last - BATTERY_PERCENT_MAX_STEP;
    }
    else
    {
        battery_filter_ready = 2; /* 第二次调用后开始限幅 */
    }

    battery_percent_last = percent;
    return percent;
}

/*
 * 统一初始化：配置 PA0（电池模拟）、PA1（光照模拟）为模拟输入，
 * 初始化 ADC1 并校准。
 * 返回：1 成功，0 失败。
 */
uint8_t ADC_Light_Battery_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_ADC1_CLK_ENABLE();

    /* PA0 - 电池模拟输入 */
    GPIO_InitStruct.Pin = BATTERY_Analog_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(BATTERY_Data_PORT, &GPIO_InitStruct);

    /* PA1 - 光照模拟输入 */
    GPIO_InitStruct.Pin = LIGHTSENSOR_Analog_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(LIGHTSENSOR_Data_PORT, &GPIO_InitStruct);

    /* ADC1 初始化 */
    hadc1.Instance = ADC1;
    hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc1.Init.ContinuousConvMode = DISABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion = 1;

    if (HAL_ADC_Init(&hadc1) != HAL_OK)
    {
        adc_initialized = 0;
        return 0;
    }

    /* ADC 校准 */
    HAL_ADCEx_Calibration_Start(&hadc1);

    adc_initialized = 1;
    return 1;
}
