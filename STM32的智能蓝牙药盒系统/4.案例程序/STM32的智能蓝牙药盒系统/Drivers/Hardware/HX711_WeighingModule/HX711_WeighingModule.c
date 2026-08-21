#include ".\Hardware\HX711_WeighingModule\HX711_WeighingModule.h"
#include ".\SYSTEM\delay\delay.h"

/**
 * @file HX711_WeighingModule.c
 * @brief HX711 称重模块驱动实现。
 */

/* 私有变量 */
static int32_t hx711_offset = 0;                   /* 去皮偏移量 */
static float hx711_calibration = 430.0f;           /* 校准系数 (需根据实际传感器标定) */
static uint8_t hx711_gain_pulses = HX711_GAIN_128; /* 默认通道A增益128 */

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
    delay_ms(100);
}

/**
 * @brief  读取HX711原始24位ADC数据
 * @retval 24位无符号原始值
 */
uint32_t HX711_ReadRaw(void)
{
    uint32_t count = 0;
    uint8_t i;

    /* 等待DOUT变低, 表示数据准备好 */
    HX711_SCK_Write(0);

    uint32_t timeout = HAL_GetTick();
    while (HX711_DOUT_Read())
    {
        if (HAL_GetTick() - timeout > 500) /* 超时500ms */
        {
            return 0;
        }
    }

    /* 读取24位数据 (MSB优先) */
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

    /* 第25~27个脉冲用于设置下次转换的增益/通道 */
    for (i = 24; i < hx711_gain_pulses; i++)
    {
        HX711_SCK_Write(1);
        delay_us(1);
        HX711_SCK_Write(0);
        delay_us(1);
    }

    /* 将24位补码转换为有符号数后再转回uint32_t */
    if (count & 0x800000)
    {
        count |= 0xFF000000; /* 符号扩展 */
    }

    return count;
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
}

/**
 * @brief  获取重量值 (单位: 克)
 * @retval 重量值, 经过去皮和校准
 */
float HX711_GetWeight(void)
{
    int32_t raw = (int32_t)HX711_ReadRaw();
    float weight = (float)(raw - hx711_offset) / hx711_calibration;

    if (weight < 0.0f)
    {
        weight = 0.0f;
    }

    return weight;
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
