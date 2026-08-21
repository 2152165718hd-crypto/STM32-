#include "./Hardware/MQ2_Smoke/MQ2_Smoke.h"

/* ==================== 私有变量 ==================== */
static ADC_HandleTypeDef MQ2_ADC_Handle; /* MQ-2烟雾传感器ADC句柄 */

/* ==================== 初始化函数 ==================== */

/**
 * @brief  MQ-2烟雾传感器初始化
 *         - DO引脚(PA2): 上拉输入，用于读取数字报警信号
 *         - AO引脚(PA3):  模拟输入，ADC1_CHANNEL_3
 * @note   MQ-2供电5V，AO输出经电阻分压后接入ADC
 *         分压比: 27/37，即ADC检测电压 = AO原始电压 * 27/37
 */
void MQ2_Smoke_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* 使能时钟 */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_ADC1_CLK_ENABLE();

    /* ---- DO引脚(PA2): 上拉输入 ---- */
    GPIO_InitStruct.Pin = MQ2_SMOKE_DO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(MQ2_SMOKE_DO_PORT, &GPIO_InitStruct);

    /* ---- AO引脚(PA3): 模拟输入 ---- */
    GPIO_InitStruct.Pin = MQ2_SMOKE_AO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(MQ2_SMOKE_AO_PORT, &GPIO_InitStruct);

    /* ---- ADC1 配置 ---- */
    MQ2_ADC_Handle.Instance = ADC1;
    MQ2_ADC_Handle.Init.ScanConvMode = ADC_SCAN_DISABLE;           /* 单通道，不扫描 */
    MQ2_ADC_Handle.Init.ContinuousConvMode = DISABLE;              /* 单次转换 */
    MQ2_ADC_Handle.Init.DiscontinuousConvMode = DISABLE;
    MQ2_ADC_Handle.Init.ExternalTrigConv = ADC_SOFTWARE_START;     /* 软件触发 */
    MQ2_ADC_Handle.Init.DataAlign = ADC_DATAALIGN_RIGHT;           /* 数据右对齐 */
    MQ2_ADC_Handle.Init.NbrOfConversion = 1;
    HAL_ADC_Init(&MQ2_ADC_Handle);

    /* ADC校准 */
    HAL_ADCEx_Calibration_Start(&MQ2_ADC_Handle);
}

/* ==================== 传感器读取函数 ==================== */

/**
 * @brief  读取MQ-2烟雾传感器DO数字输出
 * @retval 0: 检测到烟雾（DO输出低电平），1: 未检测到烟雾
 */
uint8_t MQ2_Smoke_GetDO(void)
{
    return (uint8_t)HAL_GPIO_ReadPin(MQ2_SMOKE_DO_PORT, MQ2_SMOKE_DO_PIN);
}

/**
 * @brief  读取MQ-2烟雾传感器AO分压后的ADC原始值
 * @retval 12位ADC值 (0~4095)
 */
uint16_t MQ2_Smoke_GetAO_Raw(void)
{
    ADC_ChannelConfTypeDef sConfig = {0};

    /* 配置通道 */
    sConfig.Channel = MQ2_SMOKE_CHANNEL;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5; /* 最长采样时间，提高精度 */
    HAL_ADC_ConfigChannel(&MQ2_ADC_Handle, &sConfig);

    /* 启动ADC转换 */
    HAL_ADC_Start(&MQ2_ADC_Handle);

    /* 等待转换完成 */
    HAL_ADC_PollForConversion(&MQ2_ADC_Handle, 10);

    /* 读取转换结果 */
    return (uint16_t)HAL_ADC_GetValue(&MQ2_ADC_Handle);
}

/**
 * @brief  读取ADC引脚上的分压后电压（单位：V）
 * @retval 分压后电压 (0.0 ~ 3.3V)
 */
float MQ2_Smoke_GetVoltage(void)
{
    uint16_t adc_raw = MQ2_Smoke_GetAO_Raw();
    return (float)adc_raw / 4095.0f * 3.3f;
}

/**
 * @brief  反推MQ-2 AO引脚的真实电压（补偿分压电路）
 * @note   真实电压 = 分压后电压 / (27/37) = 分压后电压 * 37/27
 * @retval AO引脚真实电压 (0.0 ~ 5.0V)
 */
float MQ2_Smoke_GetRealVoltage(void)
{
    float divider_voltage = MQ2_Smoke_GetVoltage();
    return divider_voltage / MQ2_DIVIDER_RATIO;
}
