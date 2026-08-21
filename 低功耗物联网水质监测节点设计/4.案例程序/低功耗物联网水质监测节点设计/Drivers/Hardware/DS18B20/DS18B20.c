#include ".\Hardware\DS18B20\DS18B20.h"
#include "./SYSTEM/delay/delay.h"

#define CMD_SKIP_ROM 0xCCU
#define CMD_CONVERT_T 0x44U
#define CMD_READ_SCRATCHPAD 0xBEU
#define CONVERT_WAIT_MS 750U

static void Pin_SetOutput(void)
{
    GPIO_InitTypeDef gpio = {0};

    gpio.Pin = DS18B20_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
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

static uint8_t OW_Reset(void)
{
    uint8_t presence;
    uint32_t primask;

    Pin_SetOutput();

    primask = __get_PRIMASK();
    __disable_irq();

    PIN_LOW();
    delay_us(480U);
    PIN_HIGH();

    Pin_SetInput();
    delay_us(70U);
    presence = (PIN_READ() == 0U) ? 1U : 0U;

    __set_PRIMASK(primask);

    delay_us(410U);
    return presence;
}

static void OW_WriteBit(uint8_t bit)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    Pin_SetOutput();
    PIN_LOW();

    if (bit != 0U)
    {
        delay_us(5U);
        PIN_HIGH();
        delay_us(65U);
    }
    else
    {
        delay_us(60U);
        PIN_HIGH();
        delay_us(10U);
    }

    __set_PRIMASK(primask);
}

static uint8_t OW_ReadBit(void)
{
    uint8_t bit;
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    Pin_SetOutput();
    PIN_LOW();
    delay_us(3U);

    Pin_SetInput();
    delay_us(10U);
    bit = PIN_READ();

    __set_PRIMASK(primask);

    delay_us(50U);
    return (bit != 0U) ? 1U : 0U;
}

static void OW_WriteByte(uint8_t data)
{
    for (uint8_t i = 0U; i < 8U; i++)
    {
        OW_WriteBit((uint8_t)(data & 0x01U));
        data >>= 1;
    }
}

static uint8_t OW_ReadByte(void)
{
    uint8_t data = 0U;

    for (uint8_t i = 0U; i < 8U; i++)
    {
        if (OW_ReadBit() != 0U)
        {
            data |= (uint8_t)(1U << i);
        }
    }

    return data;
}

static uint8_t CalcCRC8(const uint8_t *buf, uint8_t len)
{
    uint8_t crc = 0U;

    for (uint8_t i = 0U; i < len; i++)
    {
        uint8_t byte = buf[i];
        for (uint8_t j = 0U; j < 8U; j++)
        {
            uint8_t mix = (uint8_t)((crc ^ byte) & 0x01U);
            crc >>= 1;
            if (mix != 0U)
            {
                crc ^= 0x8CU;
            }
            byte >>= 1;
        }
    }

    return crc;
}

uint8_t DS18B20_Init(void)
{
    DS18B20_GPIO_CLK_EN();

    Pin_SetOutput();
    PIN_HIGH();

    return OW_Reset();
}

uint8_t DS18B20_StartConversion(void)
{
    if (OW_Reset() == 0U)
    {
        return 0U;
    }

    OW_WriteByte(CMD_SKIP_ROM);
    OW_WriteByte(CMD_CONVERT_T);

    return 1U;
}

float DS18B20_ReadScratchpadTemperature(void)
{
    uint8_t sp[9];
    int16_t raw;

    if (OW_Reset() == 0U)
    {
        return DS18B20_TEMP_ERROR;
    }

    OW_WriteByte(CMD_SKIP_ROM);
    OW_WriteByte(CMD_READ_SCRATCHPAD);

    for (uint8_t i = 0U; i < 9U; i++)
    {
        sp[i] = OW_ReadByte();
    }

    if (CalcCRC8(sp, 8U) != sp[8])
    {
        return DS18B20_TEMP_ERROR;
    }

    raw = (int16_t)(((uint16_t)sp[1] << 8) | sp[0]);
    return (float)raw / 16.0f;
}

float DS18B20_ReadTemperature(void)
{
    if (DS18B20_StartConversion() == 0U)
    {
        return DS18B20_TEMP_ERROR;
    }

    delay_ms(CONVERT_WAIT_MS);
    return DS18B20_ReadScratchpadTemperature();
}
