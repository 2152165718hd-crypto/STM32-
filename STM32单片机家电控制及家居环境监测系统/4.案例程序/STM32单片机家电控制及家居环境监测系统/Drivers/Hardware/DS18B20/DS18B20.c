#include ".\Hardware\DS18B20\DS18B20.h"
#include "./SYSTEM/delay/delay.h"

/* ---- 命令字 ---- */
#define CMD_SKIP_ROM 0xCC
#define CMD_CONVERT_T 0x44
#define CMD_READ_SCRATCHPAD 0xBE

/* 12 位精度最大转换时间 */
#define CONVERT_WAIT_MS 750U

/* ==================== 底层 GPIO 操作 ==================== */

static void Pin_SetOutput(void)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = DS18B20_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_OD; /* 开漏输出，更贴合单总线电气要求 */
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(DS18B20_PORT, &gpio);
}

static void Pin_SetInput(void)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = DS18B20_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(DS18B20_PORT, &gpio);
}

#define PIN_LOW() HAL_GPIO_WritePin(DS18B20_PORT, DS18B20_PIN, GPIO_PIN_RESET)
#define PIN_HIGH() HAL_GPIO_WritePin(DS18B20_PORT, DS18B20_PIN, GPIO_PIN_SET)
#define PIN_READ() ((uint8_t)HAL_GPIO_ReadPin(DS18B20_PORT, DS18B20_PIN))

/* ==================== 单总线时序 ==================== */

/**
 * @brief  复位脉冲 + 检测存在脉冲
 * @retval 1-设备应答  0-无应答
 */
static uint8_t OW_Reset(void)
{
    uint8_t presence;
    uint32_t primask;

    Pin_SetOutput();

    primask = __get_PRIMASK(); /* 保存中断状态 */
    __disable_irq();

    PIN_LOW();
    delay_us(480);
    PIN_HIGH();

    Pin_SetInput();
    delay_us(70);
    presence = (PIN_READ() == GPIO_PIN_RESET) ? 1U : 0U;

    __set_PRIMASK(primask); /* 恢复中断状态 */

    delay_us(410); /* 等待复位时序结束 */
    return presence;
}

/**
 * @brief  写一位
 */
static void OW_WriteBit(uint8_t bit)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    Pin_SetOutput();
    PIN_LOW();

    if (bit)
    {
        delay_us(5);
        PIN_HIGH();
        delay_us(65);
    }
    else
    {
        delay_us(60);
        PIN_HIGH();
        delay_us(10);
    }

    __set_PRIMASK(primask);
}

/**
 * @brief  读一位
 * @retval 0 或 1
 */
static uint8_t OW_ReadBit(void)
{
    uint8_t bit;
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    Pin_SetOutput();
    PIN_LOW();
    delay_us(3);

    Pin_SetInput();
    delay_us(10);
    bit = PIN_READ();

    __set_PRIMASK(primask);

    delay_us(50); /* 补齐时隙 */
    return bit ? 1U : 0U;
}

/* ==================== 字节级读写 ==================== */

static void OW_WriteByte(uint8_t data)
{
    for (uint8_t i = 0; i < 8; i++)
    {
        OW_WriteBit(data & 0x01U);
        data >>= 1;
    }
}

static uint8_t OW_ReadByte(void)
{
    uint8_t data = 0;
    for (uint8_t i = 0; i < 8; i++)
    {
        if (OW_ReadBit())
        {
            data |= (uint8_t)(1U << i);
        }
    }
    return data;
}

/* ==================== CRC-8 校验 ==================== */

static uint8_t CalcCRC8(const uint8_t *buf, uint8_t len)
{
    uint8_t crc = 0;
    for (uint8_t i = 0; i < len; i++)
    {
        uint8_t byte = buf[i];
        for (uint8_t j = 0; j < 8; j++)
        {
            uint8_t mix = (crc ^ byte) & 0x01U;
            crc >>= 1;
            if (mix)
                crc ^= 0x8CU;
            byte >>= 1;
        }
    }
    return crc;
}

/* ==================== 对外接口 ==================== */

uint8_t DS18B20_Init(void)
{
    DS18B20_GPIO_CLK_EN();

    Pin_SetOutput();
    PIN_HIGH();

    /* 发送复位脉冲，确认传感器在线 */
    return OW_Reset();
}

float DS18B20_ReadTemperature(void)
{
    uint8_t sp[9]; /* scratchpad 9 字节 */
    int16_t raw;

    /* ---- 启动温度转换 ---- */
    if (!OW_Reset())
        return DS18B20_TEMP_ERROR;

    OW_WriteByte(CMD_SKIP_ROM);
    OW_WriteByte(CMD_CONVERT_T);

    /* 等待 12 位转换完成（阻塞） */
    delay_ms(CONVERT_WAIT_MS);

    /* ---- 读取 Scratchpad ---- */
    if (!OW_Reset())
        return DS18B20_TEMP_ERROR;

    OW_WriteByte(CMD_SKIP_ROM);
    OW_WriteByte(CMD_READ_SCRATCHPAD);

    for (uint8_t i = 0; i < 9; i++)
        sp[i] = OW_ReadByte();

    /* CRC 校验：前 8 字节计算值应等于第 9 字节 */
    if (CalcCRC8(sp, 8) != sp[8])
        return DS18B20_TEMP_ERROR;

    /* ---- 解析温度 ---- */
    raw = (int16_t)((uint16_t)sp[1] << 8 | sp[0]);

    return (float)raw / 16.0f;
}
