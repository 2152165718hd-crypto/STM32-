#include ".\Hardware\HX711_WeighingModule\HX711_WeighingModule.h"
#include ".\SYSTEM\delay\delay.h"

/**
 * @file HX711_WeighingModule.c
 * @brief HX711 称重模块驱动实现。
 */

/* 私有变量 */
static int32_t hx711_offset = 0;                   /* 去皮偏移量 */
static uint8_t hx711_offset_valid = 0U;
static float hx711_calibration = 430.0f;           /* 校准系数 (需根据实际传感器标定) */
static uint8_t hx711_gain_pulses = HX711_GAIN_128; /* 默认通道A增益128 */
static uint32_t hx711_not_ready_since_ms = 0U;

#define HX711_NOT_READY_RECOVER_MS 1000U
#define HX711_POWER_DOWN_US 80U

/* ======================== 私有函数 ======================== */

/**
 * @brief  SCK引脚写电平
 */
static void HX711_SCK_Write(uint8_t state)
{
    HAL_GPIO_WritePin(HX711_SCK_GPIO_PORT, HX711_SCK_PIN,
                      state ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/**
 * @brief  读取DOUT引脚电平
 */
static uint8_t HX711_DOUT_Read(void)
{
    return (uint8_t)HAL_GPIO_ReadPin(HX711_DOUT_GPIO_PORT, HX711_DOUT_PIN);
}

static void HX711_RestoreIRQ(uint32_t primask)
{
    if ((primask & 1U) == 0U)
    {
        __enable_irq();
    }
}

static void HX711_ResetConversion(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    HX711_SCK_Write(1U);
    delay_us(HX711_POWER_DOWN_US);
    HX711_SCK_Write(0U);
    HX711_RestoreIRQ(primask);
}

static uint32_t HX711_ReadRawReadyData(void)
{
    uint32_t count = 0;
    uint32_t primask;
    uint8_t i;

    primask = __get_PRIMASK();
    __disable_irq();

    for (i = 0; i < 24; i++)
    {
        HX711_SCK_Write(1);
        delay_us(1);
        count = count << 1;
        if (HX711_DOUT_Read())
        {
            count++;
        }
        HX711_SCK_Write(0);
        delay_us(1);
    }

    for (i = 24; i < hx711_gain_pulses; i++)
    {
        HX711_SCK_Write(1);
        delay_us(1);
        HX711_SCK_Write(0);
        delay_us(1);
    }

    if (count & 0x800000)
    {
        count |= 0xFF000000;
    }

    HX711_RestoreIRQ(primask);
    return count;
}

static float HX711_RawToWeight(uint32_t raw)
{
    int32_t signed_raw = (int32_t)raw;
    int32_t delta;
    float calibration = hx711_calibration;

    if (hx711_offset_valid == 0U)
    {
        hx711_offset = signed_raw;
        hx711_offset_valid = 1U;
        return 0.0f;
    }

    delta = signed_raw - hx711_offset;
    if (delta < 0)
    {
        delta = -delta;
    }

    if (calibration < 0.0f)
    {
        calibration = -calibration;
    }
    if (calibration == 0.0f)
    {
        return 0.0f;
    }

    return (float)delta / calibration;
}

/* ======================== 公开函数 ======================== */

/**
 * @brief  HX711初始化 (配置SCK为输出, DOUT为输入)
 */
void HX711_Init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* SCK - 推挽输出 */
    GPIO_InitStruct.Pin = HX711_SCK_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(HX711_SCK_GPIO_PORT, &GPIO_InitStruct);

    /* DOUT - 上拉输入 */
    GPIO_InitStruct.Pin = HX711_DOUT_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(HX711_DOUT_GPIO_PORT, &GPIO_InitStruct);

    /* SCK拉低, 芯片进入正常工作模式 */
    HX711_SCK_Write(0);
    hx711_offset_valid = 0U;
    hx711_not_ready_since_ms = 0U;
    delay_ms(100);
}

/**
 * @brief  读取HX711原始24位ADC数据
 * @retval 24位无符号原始值
 */
uint32_t HX711_ReadRaw(void)
{
    /* 等待DOUT变低, 表示数据准备好 */
    HX711_SCK_Write(0);

    uint32_t timeout = HAL_GetTick();
    while (HX711_DOUT_Read())
    {
        if (HAL_GetTick() - timeout > 500) /* 超时500ms */
        {
            HX711_ResetConversion();
            return 0;
        }
    }

    return HX711_ReadRawReadyData();
}

uint8_t HX711_ReadWeightNonBlocking(float *weight_g, int32_t *raw_out)
{
    uint32_t raw;
    uint32_t now;

    if (weight_g == NULL)
    {
        return 0U;
    }

    now = HAL_GetTick();
    HX711_SCK_Write(0);
    if (HX711_DOUT_Read() != 0U)
    {
        if (hx711_not_ready_since_ms == 0U)
        {
            hx711_not_ready_since_ms = now;
        }
        else if ((uint32_t)(now - hx711_not_ready_since_ms) >= HX711_NOT_READY_RECOVER_MS)
        {
            HX711_ResetConversion();
            hx711_not_ready_since_ms = now;
        }
        return 0U;
    }

    hx711_not_ready_since_ms = 0U;
    raw = HX711_ReadRawReadyData();
    if (raw_out != NULL)
    {
        *raw_out = (int32_t)raw;
    }
    *weight_g = HX711_RawToWeight(raw);

    return 1U;
}

/**
 * @brief  去皮 (多次采样取平均作为偏移量)
 * @param  times: 采样次数 (建议10~20)
 */
void HX711_Tare(uint8_t times)
{
    int64_t sum = 0;
    uint8_t i;

    if (times == 0)
        times = 10;

    for (i = 0; i < times; i++)
    {
        sum += (int32_t)HX711_ReadRaw();
        delay_ms(10);
    }

    hx711_offset = (int32_t)(sum / times);
    hx711_offset_valid = 1U;
}

/**
 * @brief  获取重量值 (单位: 克)
 * @retval 重量值, 经过去皮和校准
 */
float HX711_GetWeight(void)
{
    int32_t raw = (int32_t)HX711_ReadRaw();
    return HX711_RawToWeight((uint32_t)raw);
}

/**
 * @brief  设置校准系数
 * @param  factor: 校准系数 (用已知重量标定得到)
 */
void HX711_SetCalibration(float factor)
{
    if (factor != 0.0f)
    {
        hx711_calibration = factor;
    }
}
