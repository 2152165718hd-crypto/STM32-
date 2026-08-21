#include "./Hardware/Flame/Flame.h"

/* ==================== 私有变量 ==================== */
static ADC_HandleTypeDef Flame_ADC_Handle; /* 火焰传感器ADC句柄 */

/* ==================== 初始化函数 ==================== */

/**
 * @brief  火焰传感器初始化
 *         - DO引脚(PA1): 上拉输入，用于读取数字报警信号
 *         - AO引脚(PA0): 模拟输入，ADC1_CHANNEL_0
 */
void Flame_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    ADC_ChannelConfTypeDef sConfig = {0};

    /* 使能时钟 */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_ADC1_CLK_ENABLE();

    /* ---- DO引脚: 上拉输入 ---- */
    GPIO_InitStruct.Pin = FLAME_DO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(FLAME_DO_PORT, &GPIO_InitStruct);

    /* ---- AO引脚: 模拟输入 ---- */
    GPIO_InitStruct.Pin = FLAME_AO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(FLAME_AO_PORT, &GPIO_InitStruct);

    /* ---- ADC1 配置 ---- */
    Flame_ADC_Handle.Instance = ADC1;
    Flame_ADC_Handle.Init.ScanConvMode = ADC_SCAN_DISABLE;           /* 单通道，不扫描 */
    Flame_ADC_Handle.Init.ContinuousConvMode = DISABLE;              /* 单次转换 */
    Flame_ADC_Handle.Init.DiscontinuousConvMode = DISABLE;
    Flame_ADC_Handle.Init.ExternalTrigConv = ADC_SOFTWARE_START;     /* 软件触发 */
    Flame_ADC_Handle.Init.DataAlign = ADC_DATAALIGN_RIGHT;           /* 数据右对齐 */
    Flame_ADC_Handle.Init.NbrOfConversion = 1;
    HAL_ADC_Init(&Flame_ADC_Handle);

    /* ADC校准 */
    HAL_ADCEx_Calibration_Start(&Flame_ADC_Handle);
}

/* ==================== 传感器读取函数 ==================== */

/**
 * @brief  读取火焰传感器DO数字输出
 * @retval 0: 检测到火焰（DO输出低电平），1: 未检测到火焰
 */
uint8_t Flame_GetDO(void)
{
    /* Active-low DO: return 1 when flame is detected. */
    return (HAL_GPIO_ReadPin(FLAME_DO_PORT, FLAME_DO_PIN) == GPIO_PIN_RESET) ? 1U : 0U;
}

/**
 * @brief  读取火焰传感器AO模拟输出的ADC原始值
 * @retval 12位ADC值 (0~4095)
 */
uint16_t Flame_GetAO_Raw(void)
{
    ADC_ChannelConfTypeDef sConfig = {0};

    /* 配置通道 */
    sConfig.Channel = FLAME_CHANNEL;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5; /* 最长采样时间，提高精度 */
    HAL_ADC_ConfigChannel(&Flame_ADC_Handle, &sConfig);

    /* 启动ADC转换 */
    HAL_ADC_Start(&Flame_ADC_Handle);

    /* 等待转换完成 */
    HAL_ADC_PollForConversion(&Flame_ADC_Handle, 10);

    /* 读取转换结果 */
    return (uint16_t)HAL_ADC_GetValue(&Flame_ADC_Handle);
}

/**
 * @brief  读取火焰传感器AO引脚的实际电压（单位：V）
 * @note   ADC参考电压为3.3V，12位分辨率
 * @retval 电压值 (0.0 ~ 3.3V)
 */
float Flame_GetVoltage(void)
{
    uint16_t adc_raw = Flame_GetAO_Raw();
    return (float)adc_raw / 4095.0f * 3.3f;
}

uint8_t Flame_GetAO_Percent(void)
{
    uint16_t adc_raw = Flame_GetAO_Raw();
    /* AO decreases when flame is stronger, so use inverse mapping. */
    uint32_t percent = ((uint32_t)(4095U - adc_raw) * 100U + 2047U) / 4095U;

    if (percent > 100U)
    {
        percent = 100U;
    }

    return (uint8_t)percent;
}
