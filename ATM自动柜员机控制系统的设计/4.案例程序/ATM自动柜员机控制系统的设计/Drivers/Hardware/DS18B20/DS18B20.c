#include ".\Hardware\DS18B20\DS18B20.h"
#include ".\SYSTEM\delay\delay.h"

#define CMD_SKIP_ROM      0xCC
#define CMD_CONVERT_T     0x44
#define CMD_READ_SCRATCH  0xBE
#define CONVERT_WAIT_MS   750u

static uint8_t s_ds18_ready = 0u;

static void DS18_PinSetOutput(void)
{
    GPIO_InitTypeDef gpio = {0};

    gpio.Pin = DS18B20_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(DS18B20_PORT, &gpio);
}

static void DS18_PinSetInput(void)
{
    GPIO_InitTypeDef gpio = {0};

    gpio.Pin = DS18B20_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(DS18B20_PORT, &gpio);
}

#define DS18_LOW()  HAL_GPIO_WritePin(DS18B20_PORT, DS18B20_PIN, GPIO_PIN_RESET)
#define DS18_HIGH() HAL_GPIO_WritePin(DS18B20_PORT, DS18B20_PIN, GPIO_PIN_SET)
#define DS18_READ() ((uint8_t)HAL_GPIO_ReadPin(DS18B20_PORT, DS18B20_PIN))

static uint8_t DS18_Reset(void)
{
    uint8_t presence;
    uint32_t primask;

    DS18_PinSetOutput();

    primask = __get_PRIMASK();
    __disable_irq();

    DS18_LOW();
    delay_us(480);
    DS18_HIGH();

    DS18_PinSetInput();
    delay_us(70);
    presence = (DS18_READ() == GPIO_PIN_RESET) ? 1u : 0u;

    __set_PRIMASK(primask);
    delay_us(410);
    return presence;
}

static void DS18_WriteBit(uint8_t bit)
{
    uint32_t primask;

    primask = __get_PRIMASK();
    __disable_irq();

    DS18_PinSetOutput();
    DS18_LOW();

    if (bit != 0u)
    {
        delay_us(5);
        DS18_HIGH();
        delay_us(65);
    }
    else
    {
        delay_us(60);
        DS18_HIGH();
        delay_us(10);
    }

    __set_PRIMASK(primask);
}

static uint8_t DS18_ReadBit(void)
{
    uint8_t bit;
    uint32_t primask;

    primask = __get_PRIMASK();
    __disable_irq();

    DS18_PinSetOutput();
    DS18_LOW();
    delay_us(3);

    DS18_PinSetInput();
    delay_us(10);
    bit = DS18_READ();

    __set_PRIMASK(primask);
    delay_us(50);
    return (bit != 0u) ? 1u : 0u;
}

static void DS18_WriteByte(uint8_t data)
{
    uint8_t i;

    for (i = 0u; i < 8u; i++)
    {
        DS18_WriteBit(data & 0x01u);
        data >>= 1;
    }
}

static uint8_t DS18_ReadByte(void)
{
    uint8_t i;
    uint8_t data = 0u;

    for (i = 0u; i < 8u; i++)
    {
        if (DS18_ReadBit() != 0u)
        {
            data |= (uint8_t)(1u << i);
        }
    }

    return data;
}

static uint8_t DS18_CalcCrc8(const uint8_t *buf, uint8_t len)
{
    uint8_t crc = 0u;
    uint8_t i;
    uint8_t j;

    for (i = 0u; i < len; i++)
    {
        uint8_t byte = buf[i];
        for (j = 0u; j < 8u; j++)
        {
            uint8_t mix = (crc ^ byte) & 0x01u;
            crc >>= 1;
            if (mix != 0u)
            {
                crc ^= 0x8Cu;
            }
            byte >>= 1;
        }
    }

    return crc;
}

uint8_t DS18B20_Init(void)
{
    DS18B20_GPIO_CLK_EN();
    DS18_PinSetOutput();
    DS18_HIGH();
    s_ds18_ready = DS18_Reset();
    return s_ds18_ready;
}

uint8_t DS18B20_IsReady(void)
{
    return s_ds18_ready;
}

uint8_t DS18B20_StartConversion(void)
{
    if (DS18_Reset() == 0u)
    {
        s_ds18_ready = 0u;
        return 0u;
    }

    s_ds18_ready = 1u;
    DS18_WriteByte(CMD_SKIP_ROM);
    DS18_WriteByte(CMD_CONVERT_T);
    return 1u;
}

float DS18B20_ReadTemperatureResult(void)
{
    uint8_t scratch[9];
    uint8_t i;
    int16_t raw;

    if (DS18_Reset() == 0u)
    {
        s_ds18_ready = 0u;
        return DS18B20_TEMP_ERROR;
    }

    s_ds18_ready = 1u;
    DS18_WriteByte(CMD_SKIP_ROM);
    DS18_WriteByte(CMD_READ_SCRATCH);

    for (i = 0u; i < 9u; i++)
    {
        scratch[i] = DS18_ReadByte();
    }

    if (DS18_CalcCrc8(scratch, 8u) != scratch[8])
    {
        return DS18B20_TEMP_ERROR;
    }

    raw = (int16_t)(((uint16_t)scratch[1] << 8) | scratch[0]);
    return (float)raw / 16.0f;
}

float DS18B20_ReadTemperature(void)
{
    if (DS18B20_StartConversion() == 0u)
    {
        return DS18B20_TEMP_ERROR;
    }

    delay_ms(CONVERT_WAIT_MS);
    return DS18B20_ReadTemperatureResult();
}
