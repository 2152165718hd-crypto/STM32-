#include "HARDWARE/AT24C256/AT24C256.h"

#define AT24C256_DEV_ADDR ((uint16_t)(AT24C256_ADDR_7BIT << 1U))
#define AT24C256_READY_TRIALS 20U
#define AT24C256_READY_TIMEOUT 5U
#define AT24C256_IO_TIMEOUT 100U

static I2C_HandleTypeDef s_at24_i2c;

void AT24C256_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    AT24C256_I2C_GPIO_CLK_ENABLE();
    AT24C256_I2C_CLK_ENABLE();

    GPIO_InitStruct.Pin = AT24C256_I2C_SCL_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = AT24C256_I2C_AF;
    HAL_GPIO_Init(AT24C256_I2C_SCL_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = AT24C256_I2C_SDA_PIN;
    HAL_GPIO_Init(AT24C256_I2C_SDA_PORT, &GPIO_InitStruct);

    s_at24_i2c.Instance = AT24C256_I2C;
    s_at24_i2c.Init.ClockSpeed = 100000U;
    s_at24_i2c.Init.DutyCycle = I2C_DUTYCYCLE_2;
    s_at24_i2c.Init.OwnAddress1 = 0U;
    s_at24_i2c.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    s_at24_i2c.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    s_at24_i2c.Init.OwnAddress2 = 0U;
    s_at24_i2c.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    s_at24_i2c.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    (void)HAL_I2C_Init(&s_at24_i2c);
}

HAL_StatusTypeDef AT24C256_IsReady(void)
{
    return HAL_I2C_IsDeviceReady(&s_at24_i2c, AT24C256_DEV_ADDR, AT24C256_READY_TRIALS, AT24C256_READY_TIMEOUT);
}

HAL_StatusTypeDef AT24C256_WriteByte(uint16_t mem_addr, uint8_t data)
{
    return AT24C256_Write(mem_addr, &data, 1U);
}

HAL_StatusTypeDef AT24C256_ReadByte(uint16_t mem_addr, uint8_t *data)
{
    return AT24C256_Read(mem_addr, data, 1U);
}

HAL_StatusTypeDef AT24C256_Write(uint16_t mem_addr, const uint8_t *data, uint16_t len)
{
    uint16_t remain;
    uint16_t cur_addr;
    const uint8_t *cur_ptr;

    if ((data == NULL) || (len == 0U))
    {
        return HAL_ERROR;
    }

    if (((uint32_t)mem_addr + (uint32_t)len) > AT24C256_TOTAL_SIZE)
    {
        return HAL_ERROR;
    }

    remain = len;
    cur_addr = mem_addr;
    cur_ptr = data;

    while (remain > 0U)
    {
        uint16_t page_offset = (uint16_t)(cur_addr % AT24C256_PAGE_SIZE);
        uint16_t page_space = (uint16_t)(AT24C256_PAGE_SIZE - page_offset);
        uint16_t chunk = (remain < page_space) ? remain : page_space;

        if (HAL_I2C_Mem_Write(&s_at24_i2c, AT24C256_DEV_ADDR, cur_addr, I2C_MEMADD_SIZE_16BIT,
                              (uint8_t *)cur_ptr, chunk, AT24C256_IO_TIMEOUT) != HAL_OK)
        {
            return HAL_ERROR;
        }

        if (AT24C256_IsReady() != HAL_OK)
        {
            return HAL_TIMEOUT;
        }

        cur_addr = (uint16_t)(cur_addr + chunk);
        cur_ptr += chunk;
        remain = (uint16_t)(remain - chunk);
    }

    return HAL_OK;
}

HAL_StatusTypeDef AT24C256_Read(uint16_t mem_addr, uint8_t *data, uint16_t len)
{
    if ((data == NULL) || (len == 0U))
    {
        return HAL_ERROR;
    }

    if (((uint32_t)mem_addr + (uint32_t)len) > AT24C256_TOTAL_SIZE)
    {
        return HAL_ERROR;
    }

    return HAL_I2C_Mem_Read(&s_at24_i2c, AT24C256_DEV_ADDR, mem_addr, I2C_MEMADD_SIZE_16BIT,
                            data, len, AT24C256_IO_TIMEOUT);
}

I2C_HandleTypeDef *AT24C256_GetI2CHandle(void)
{
    return &s_at24_i2c;
}
