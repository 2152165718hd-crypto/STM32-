#include "HARDWARE/PCF8563/PCF8563.h"
#include "SYSTEM/delay/delay.h"

#define PCF8563_DEV_ADDR ((uint16_t)(PCF8563_ADDR_7BIT << 1U))
#define PCF8563_REG_CONTROL1 0x00U
#define PCF8563_REG_SECONDS 0x02U
#define PCF8563_IO_TIMEOUT 100U

static I2C_HandleTypeDef s_pcf_i2c;
static PCF8563_Time_t s_lastTime;
static PCF8563_I2C_Mode_t s_i2c_mode = PCF8563_I2C_MODE_DEFAULT;

static uint8_t PCF8563_BcdToDec(uint8_t bcd)
{
    return (uint8_t)(((bcd >> 4) * 10U) + (bcd & 0x0FU));
}

static uint8_t PCF8563_DecToBcd(uint8_t dec)
{
    return (uint8_t)(((dec / 10U) << 4) | (dec % 10U));
}

static void PCF8563_SoftI2C_Delay(void)
{
    delay_us(PCF8563_SOFT_I2C_DELAY_US);
}

static void PCF8563_SoftI2C_SCL_High(void)
{
    HAL_GPIO_WritePin(PCF8563_SOFT_I2C_SCL_PORT, PCF8563_SOFT_I2C_SCL_PIN, GPIO_PIN_SET);
}

static void PCF8563_SoftI2C_SCL_Low(void)
{
    HAL_GPIO_WritePin(PCF8563_SOFT_I2C_SCL_PORT, PCF8563_SOFT_I2C_SCL_PIN, GPIO_PIN_RESET);
}

static void PCF8563_SoftI2C_SDA_High(void)
{
    HAL_GPIO_WritePin(PCF8563_SOFT_I2C_SDA_PORT, PCF8563_SOFT_I2C_SDA_PIN, GPIO_PIN_SET);
}

static void PCF8563_SoftI2C_SDA_Low(void)
{
    HAL_GPIO_WritePin(PCF8563_SOFT_I2C_SDA_PORT, PCF8563_SOFT_I2C_SDA_PIN, GPIO_PIN_RESET);
}

static uint8_t PCF8563_SoftI2C_SDA_Read(void)
{
    return (HAL_GPIO_ReadPin(PCF8563_SOFT_I2C_SDA_PORT, PCF8563_SOFT_I2C_SDA_PIN) == GPIO_PIN_SET) ? 1U : 0U;
}

static void PCF8563_SoftI2C_Start(void)
{
    PCF8563_SoftI2C_SDA_High();
    PCF8563_SoftI2C_SCL_High();
    PCF8563_SoftI2C_Delay();
    PCF8563_SoftI2C_SDA_Low();
    PCF8563_SoftI2C_Delay();
    PCF8563_SoftI2C_SCL_Low();
}

static void PCF8563_SoftI2C_Stop(void)
{
    PCF8563_SoftI2C_SCL_Low();
    PCF8563_SoftI2C_SDA_Low();
    PCF8563_SoftI2C_Delay();
    PCF8563_SoftI2C_SCL_High();
    PCF8563_SoftI2C_Delay();
    PCF8563_SoftI2C_SDA_High();
    PCF8563_SoftI2C_Delay();
}

static uint8_t PCF8563_SoftI2C_WaitAck(void)
{
    uint32_t timeout = PCF8563_SOFT_I2C_TIMEOUT;

    PCF8563_SoftI2C_SDA_High();
    PCF8563_SoftI2C_Delay();
    PCF8563_SoftI2C_SCL_High();
    PCF8563_SoftI2C_Delay();

    while (PCF8563_SoftI2C_SDA_Read() != 0U)
    {
        if (timeout-- == 0U)
        {
            PCF8563_SoftI2C_SCL_Low();
            return 0U;
        }
    }

    PCF8563_SoftI2C_SCL_Low();
    PCF8563_SoftI2C_Delay();
    return 1U;
}

static void PCF8563_SoftI2C_SendAck(void)
{
    PCF8563_SoftI2C_SDA_Low();
    PCF8563_SoftI2C_Delay();
    PCF8563_SoftI2C_SCL_High();
    PCF8563_SoftI2C_Delay();
    PCF8563_SoftI2C_SCL_Low();
    PCF8563_SoftI2C_Delay();
    PCF8563_SoftI2C_SDA_High();
}

static void PCF8563_SoftI2C_SendNack(void)
{
    PCF8563_SoftI2C_SDA_High();
    PCF8563_SoftI2C_Delay();
    PCF8563_SoftI2C_SCL_High();
    PCF8563_SoftI2C_Delay();
    PCF8563_SoftI2C_SCL_Low();
    PCF8563_SoftI2C_Delay();
}

static uint8_t PCF8563_SoftI2C_WriteByte(uint8_t data)
{
    uint8_t bit;

    for (bit = 0U; bit < 8U; bit++)
    {
        if ((data & 0x80U) != 0U)
        {
            PCF8563_SoftI2C_SDA_High();
        }
        else
        {
            PCF8563_SoftI2C_SDA_Low();
        }

        PCF8563_SoftI2C_Delay();
        PCF8563_SoftI2C_SCL_High();
        PCF8563_SoftI2C_Delay();
        PCF8563_SoftI2C_SCL_Low();
        PCF8563_SoftI2C_Delay();
        data <<= 1U;
    }

    return PCF8563_SoftI2C_WaitAck();
}

static uint8_t PCF8563_SoftI2C_ReadByte(uint8_t ack)
{
    uint8_t bit;
    uint8_t data = 0U;

    PCF8563_SoftI2C_SDA_High();
    for (bit = 0U; bit < 8U; bit++)
    {
        data <<= 1U;
        PCF8563_SoftI2C_SCL_High();
        PCF8563_SoftI2C_Delay();
        if (PCF8563_SoftI2C_SDA_Read() != 0U)
        {
            data |= 0x01U;
        }
        PCF8563_SoftI2C_SCL_Low();
        PCF8563_SoftI2C_Delay();
    }

    if (ack != 0U)
    {
        PCF8563_SoftI2C_SendAck();
    }
    else
    {
        PCF8563_SoftI2C_SendNack();
    }

    return data;
}

static void PCF8563_SoftI2C_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    PCF8563_SOFT_I2C_SCL_GPIO_CLK_ENABLE();
    PCF8563_SOFT_I2C_SDA_GPIO_CLK_ENABLE();

    HAL_GPIO_WritePin(PCF8563_SOFT_I2C_SCL_PORT, PCF8563_SOFT_I2C_SCL_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(PCF8563_SOFT_I2C_SDA_PORT, PCF8563_SOFT_I2C_SDA_PIN, GPIO_PIN_SET);

    GPIO_InitStruct.Pin = PCF8563_SOFT_I2C_SCL_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(PCF8563_SOFT_I2C_SCL_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = PCF8563_SOFT_I2C_SDA_PIN;
    HAL_GPIO_Init(PCF8563_SOFT_I2C_SDA_PORT, &GPIO_InitStruct);
}

static HAL_StatusTypeDef PCF8563_SoftI2C_IsReady(void)
{
    uint8_t ok;

    PCF8563_SoftI2C_Start();
    ok = PCF8563_SoftI2C_WriteByte((uint8_t)PCF8563_DEV_ADDR);
    PCF8563_SoftI2C_Stop();

    return (ok != 0U) ? HAL_OK : HAL_ERROR;
}

static HAL_StatusTypeDef PCF8563_SoftI2C_Mem_Write(uint8_t reg, const uint8_t *buf, uint16_t len)
{
    uint16_t i;

    if ((buf == NULL) || (len == 0U))
    {
        return HAL_ERROR;
    }

    PCF8563_SoftI2C_Start();
    if (PCF8563_SoftI2C_WriteByte((uint8_t)PCF8563_DEV_ADDR) == 0U)
    {
        PCF8563_SoftI2C_Stop();
        return HAL_ERROR;
    }
    if (PCF8563_SoftI2C_WriteByte(reg) == 0U)
    {
        PCF8563_SoftI2C_Stop();
        return HAL_ERROR;
    }

    for (i = 0U; i < len; i++)
    {
        if (PCF8563_SoftI2C_WriteByte(buf[i]) == 0U)
        {
            PCF8563_SoftI2C_Stop();
            return HAL_ERROR;
        }
    }

    PCF8563_SoftI2C_Stop();
    return HAL_OK;
}

static HAL_StatusTypeDef PCF8563_SoftI2C_Mem_Read(uint8_t reg, uint8_t *buf, uint16_t len)
{
    uint16_t i;

    if ((buf == NULL) || (len == 0U))
    {
        return HAL_ERROR;
    }

    PCF8563_SoftI2C_Start();
    if (PCF8563_SoftI2C_WriteByte((uint8_t)PCF8563_DEV_ADDR) == 0U)
    {
        PCF8563_SoftI2C_Stop();
        return HAL_ERROR;
    }
    if (PCF8563_SoftI2C_WriteByte(reg) == 0U)
    {
        PCF8563_SoftI2C_Stop();
        return HAL_ERROR;
    }

    PCF8563_SoftI2C_Start();
    if (PCF8563_SoftI2C_WriteByte((uint8_t)(PCF8563_DEV_ADDR | 0x01U)) == 0U)
    {
        PCF8563_SoftI2C_Stop();
        return HAL_ERROR;
    }

    for (i = 0U; i < len; i++)
    {
        uint8_t ack = (i + 1U < len) ? 1U : 0U;
        buf[i] = PCF8563_SoftI2C_ReadByte(ack);
    }

    PCF8563_SoftI2C_Stop();
    return HAL_OK;
}

static void PCF8563_HardI2C_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    PCF8563_I2C_SCL_GPIO_CLK_ENABLE();
    PCF8563_I2C_SDA_GPIO_CLK_ENABLE();
    PCF8563_I2C_CLK_ENABLE();

    GPIO_InitStruct.Pin = PCF8563_I2C_SCL_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = PCF8563_I2C_AF;
    HAL_GPIO_Init(PCF8563_I2C_SCL_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = PCF8563_I2C_SDA_PIN;
    HAL_GPIO_Init(PCF8563_I2C_SDA_PORT, &GPIO_InitStruct);

    s_pcf_i2c.Instance = PCF8563_I2C;
    s_pcf_i2c.Init.ClockSpeed = 100000U;
    s_pcf_i2c.Init.DutyCycle = I2C_DUTYCYCLE_2;
    s_pcf_i2c.Init.OwnAddress1 = 0U;
    s_pcf_i2c.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    s_pcf_i2c.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    s_pcf_i2c.Init.OwnAddress2 = 0U;
    s_pcf_i2c.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    s_pcf_i2c.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    (void)HAL_I2C_Init(&s_pcf_i2c);
}

static HAL_StatusTypeDef PCF8563_I2C_Mem_Write(uint8_t reg, const uint8_t *buf, uint16_t len)
{
    if (s_i2c_mode == PCF8563_I2C_MODE_SOFT)
    {
        return PCF8563_SoftI2C_Mem_Write(reg, buf, len);
    }

    return HAL_I2C_Mem_Write(&s_pcf_i2c, PCF8563_DEV_ADDR, reg, I2C_MEMADD_SIZE_8BIT,
                             (uint8_t *)buf, len, PCF8563_IO_TIMEOUT);
}

static HAL_StatusTypeDef PCF8563_I2C_Mem_Read(uint8_t reg, uint8_t *buf, uint16_t len)
{
    if (s_i2c_mode == PCF8563_I2C_MODE_SOFT)
    {
        return PCF8563_SoftI2C_Mem_Read(reg, buf, len);
    }

    return HAL_I2C_Mem_Read(&s_pcf_i2c, PCF8563_DEV_ADDR, reg, I2C_MEMADD_SIZE_8BIT,
                            buf, len, PCF8563_IO_TIMEOUT);
}

static HAL_StatusTypeDef PCF8563_I2C_IsReady(void)
{
    if (s_i2c_mode == PCF8563_I2C_MODE_SOFT)
    {
        return PCF8563_SoftI2C_IsReady();
    }

    return HAL_I2C_IsDeviceReady(&s_pcf_i2c, PCF8563_DEV_ADDR, 3U, PCF8563_IO_TIMEOUT);
}

void PCF8563_SetI2CMode(PCF8563_I2C_Mode_t mode)
{
    s_i2c_mode = mode;
}

PCF8563_I2C_Mode_t PCF8563_GetI2CMode(void)
{
    return s_i2c_mode;
}

void PCF8563_Init(void)
{
    uint8_t ctrl[2] = {0U, 0U};

    if (s_i2c_mode == PCF8563_I2C_MODE_SOFT)
    {
        PCF8563_SoftI2C_Init();
    }
    else
    {
        PCF8563_HardI2C_Init();
    }

    (void)PCF8563_I2C_Mem_Write(PCF8563_REG_CONTROL1, ctrl, sizeof(ctrl));
}

HAL_StatusTypeDef PCF8563_IsReady(void)
{
    return PCF8563_I2C_IsReady();
}

HAL_StatusTypeDef PCF8563_GetTime(PCF8563_Time_t *time)
{
    uint8_t buf[7];

    if (time == NULL)
    {
        return HAL_ERROR;
    }

    if (PCF8563_I2C_Mem_Read(PCF8563_REG_SECONDS, buf, sizeof(buf)) != HAL_OK)
    {
        time->valid = 0U;
        return HAL_ERROR;
    }

    time->valid = ((buf[0] & 0x80U) == 0U) ? 1U : 0U;
    time->second = PCF8563_BcdToDec((uint8_t)(buf[0] & 0x7FU));
    time->minute = PCF8563_BcdToDec((uint8_t)(buf[1] & 0x7FU));
    time->hour = PCF8563_BcdToDec((uint8_t)(buf[2] & 0x3FU));
    time->day = PCF8563_BcdToDec((uint8_t)(buf[3] & 0x3FU));
    time->weekday = PCF8563_BcdToDec((uint8_t)(buf[4] & 0x07U));
    time->month = PCF8563_BcdToDec((uint8_t)(buf[5] & 0x1FU));
    time->year = PCF8563_BcdToDec(buf[6]);

    s_lastTime = *time;
    return HAL_OK;
}

HAL_StatusTypeDef PCF8563_SetTime(const PCF8563_Time_t *time)
{
    uint8_t buf[7];

    if (time == NULL)
    {
        return HAL_ERROR;
    }

    buf[0] = PCF8563_DecToBcd(time->second);
    buf[1] = PCF8563_DecToBcd(time->minute);
    buf[2] = PCF8563_DecToBcd(time->hour);
    buf[3] = PCF8563_DecToBcd(time->day);
    buf[4] = PCF8563_DecToBcd(time->weekday);
    buf[5] = PCF8563_DecToBcd(time->month);
    buf[6] = PCF8563_DecToBcd(time->year);

    return PCF8563_I2C_Mem_Write(PCF8563_REG_SECONDS, buf, sizeof(buf));
}

I2C_HandleTypeDef *PCF8563_GetI2CHandle(void)
{
    return (s_i2c_mode == PCF8563_I2C_MODE_HARD) ? &s_pcf_i2c : NULL;
}

void P8563_init(void)
{
    PCF8563_Init();
}

void P8563_gettime(void)
{
    (void)PCF8563_GetTime(&s_lastTime);
}
