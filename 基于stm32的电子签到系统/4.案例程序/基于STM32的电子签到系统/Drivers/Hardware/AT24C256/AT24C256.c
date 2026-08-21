#include ".\Hardware\AT24C256\AT24C256.h"
#include ".\SYSTEM\delay\delay.h"

#define AT24C256_DEV_ADDR_WRITE ((uint8_t)(AT24C256_ADDR_7BIT << 1))
#define AT24C256_DEV_ADDR_READ ((uint8_t)((AT24C256_ADDR_7BIT << 1) | 0x01u))

#define AT24C256_I2C_DELAY_US 4u
#define AT24C256_READY_RETRY 20u

static void AT24C256_EnableGPIOClock(GPIO_TypeDef *port)
{
    if (port == GPIOA)
    {
        __HAL_RCC_GPIOA_CLK_ENABLE();
    }
    else if (port == GPIOB)
    {
        __HAL_RCC_GPIOB_CLK_ENABLE();
    }
    else if (port == GPIOC)
    {
        __HAL_RCC_GPIOC_CLK_ENABLE();
    }
    else if (port == GPIOD)
    {
        __HAL_RCC_GPIOD_CLK_ENABLE();
    }
    else if (port == GPIOE)
    {
        __HAL_RCC_GPIOE_CLK_ENABLE();
    }
}

static void AT24C256_SCL(uint8_t level)
{
    HAL_GPIO_WritePin(AT24C256_I2C_PORT, AT24C256_I2C_SCL_PIN, (GPIO_PinState)level);
}

static void AT24C256_SDA(uint8_t level)
{
    HAL_GPIO_WritePin(AT24C256_I2C_PORT, AT24C256_I2C_SDA_PIN, (GPIO_PinState)level);
}

static uint8_t AT24C256_SDA_Read(void)
{
    return (HAL_GPIO_ReadPin(AT24C256_I2C_PORT, AT24C256_I2C_SDA_PIN) == GPIO_PIN_SET) ? 1u : 0u;
}

static void AT24C256_I2C_Delay(void)
{
    delay_us(AT24C256_I2C_DELAY_US);
}

static void AT24C256_I2C_Start(void)
{
    AT24C256_SDA(1u);
    AT24C256_SCL(1u);
    AT24C256_I2C_Delay();
    AT24C256_SDA(0u);
    AT24C256_I2C_Delay();
    AT24C256_SCL(0u);
}

static void AT24C256_I2C_Stop(void)
{
    AT24C256_SDA(0u);
    AT24C256_I2C_Delay();
    AT24C256_SCL(1u);
    AT24C256_I2C_Delay();
    AT24C256_SDA(1u);
    AT24C256_I2C_Delay();
}

static uint8_t AT24C256_I2C_WriteByte(uint8_t data)
{
    uint8_t i;
    for (i = 0u; i < 8u; i++)
    {
        AT24C256_SDA((data & 0x80u) ? 1u : 0u);
        AT24C256_I2C_Delay();
        AT24C256_SCL(1u);
        AT24C256_I2C_Delay();
        AT24C256_SCL(0u);
        data <<= 1u;
    }

    AT24C256_SDA(1u);
    AT24C256_I2C_Delay();
    AT24C256_SCL(1u);
    AT24C256_I2C_Delay();
    i = (uint8_t)(AT24C256_SDA_Read() == 0u);
    AT24C256_SCL(0u);
    AT24C256_I2C_Delay();

    return i;
}

static uint8_t AT24C256_I2C_ReadByte(uint8_t ack)
{
    uint8_t i;
    uint8_t data = 0u;

    AT24C256_SDA(1u);
    for (i = 0u; i < 8u; i++)
    {
        data <<= 1u;
        AT24C256_SCL(1u);
        AT24C256_I2C_Delay();
        if (AT24C256_SDA_Read())
        {
            data |= 0x01u;
        }
        AT24C256_SCL(0u);
        AT24C256_I2C_Delay();
    }

    AT24C256_SDA(ack ? 0u : 1u);
    AT24C256_I2C_Delay();
    AT24C256_SCL(1u);
    AT24C256_I2C_Delay();
    AT24C256_SCL(0u);
    AT24C256_SDA(1u);

    return data;
}

static HAL_StatusTypeDef AT24C256_WaitReady(void)
{
    uint8_t retry;

    for (retry = 0u; retry < AT24C256_READY_RETRY; retry++)
    {
        AT24C256_I2C_Start();
        if (AT24C256_I2C_WriteByte(AT24C256_DEV_ADDR_WRITE))
        {
            AT24C256_I2C_Stop();
            return HAL_OK;
        }
        AT24C256_I2C_Stop();
        delay_ms(1u);
    }

    return HAL_TIMEOUT;
}

static HAL_StatusTypeDef AT24C256_WritePage(uint16_t mem_addr, const uint8_t *data, uint8_t len)
{
    uint8_t i;

    AT24C256_I2C_Start();
    if (!AT24C256_I2C_WriteByte(AT24C256_DEV_ADDR_WRITE))
    {
        AT24C256_I2C_Stop();
        return HAL_ERROR;
    }
    if (!AT24C256_I2C_WriteByte((uint8_t)(mem_addr >> 8)))
    {
        AT24C256_I2C_Stop();
        return HAL_ERROR;
    }
    if (!AT24C256_I2C_WriteByte((uint8_t)(mem_addr & 0xFFu)))
    {
        AT24C256_I2C_Stop();
        return HAL_ERROR;
    }

    for (i = 0u; i < len; i++)
    {
        if (!AT24C256_I2C_WriteByte(data[i]))
        {
            AT24C256_I2C_Stop();
            return HAL_ERROR;
        }
    }

    AT24C256_I2C_Stop();
    return AT24C256_WaitReady();
}

void AT24C256_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    AT24C256_EnableGPIOClock(AT24C256_I2C_PORT);

    GPIO_InitStruct.Pin = AT24C256_I2C_SCL_PIN | AT24C256_I2C_SDA_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(AT24C256_I2C_PORT, &GPIO_InitStruct);

    AT24C256_SDA(1u);
    AT24C256_SCL(1u);
}

HAL_StatusTypeDef AT24C256_IsReady(void)
{
    return AT24C256_WaitReady();
}

HAL_StatusTypeDef AT24C256_WriteByte(uint16_t mem_addr, uint8_t data)
{
    return AT24C256_Write(mem_addr, &data, 1u);
}

HAL_StatusTypeDef AT24C256_ReadByte(uint16_t mem_addr, uint8_t *data)
{
    return AT24C256_Read(mem_addr, data, 1u);
}

HAL_StatusTypeDef AT24C256_Write(uint16_t mem_addr, const uint8_t *data, uint16_t len)
{
    uint16_t remain;
    uint16_t cur_addr;
    const uint8_t *cur_ptr;

    if ((data == NULL) || (len == 0u))
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

    while (remain > 0u)
    {
        uint8_t page_off = (uint8_t)(cur_addr % AT24C256_PAGE_SIZE);
        uint8_t page_space = (uint8_t)(AT24C256_PAGE_SIZE - page_off);
        uint8_t chunk = (remain < page_space) ? (uint8_t)remain : page_space;

        if (AT24C256_WritePage(cur_addr, cur_ptr, chunk) != HAL_OK)
        {
            return HAL_ERROR;
        }

        cur_addr = (uint16_t)(cur_addr + chunk);
        cur_ptr += chunk;
        remain = (uint16_t)(remain - chunk);
    }

    return HAL_OK;
}

HAL_StatusTypeDef AT24C256_Read(uint16_t mem_addr, uint8_t *data, uint16_t len)
{
    uint16_t i;

    if ((data == NULL) || (len == 0u))
    {
        return HAL_ERROR;
    }
    if (((uint32_t)mem_addr + (uint32_t)len) > AT24C256_TOTAL_SIZE)
    {
        return HAL_ERROR;
    }

    AT24C256_I2C_Start();
    if (!AT24C256_I2C_WriteByte(AT24C256_DEV_ADDR_WRITE))
    {
        AT24C256_I2C_Stop();
        return HAL_ERROR;
    }
    if (!AT24C256_I2C_WriteByte((uint8_t)(mem_addr >> 8)))
    {
        AT24C256_I2C_Stop();
        return HAL_ERROR;
    }
    if (!AT24C256_I2C_WriteByte((uint8_t)(mem_addr & 0xFFu)))
    {
        AT24C256_I2C_Stop();
        return HAL_ERROR;
    }

    AT24C256_I2C_Start();
    if (!AT24C256_I2C_WriteByte(AT24C256_DEV_ADDR_READ))
    {
        AT24C256_I2C_Stop();
        return HAL_ERROR;
    }

    for (i = 0u; i < len; i++)
    {
        data[i] = AT24C256_I2C_ReadByte((i < (len - 1u)) ? 1u : 0u);
    }

    AT24C256_I2C_Stop();
    return HAL_OK;
}
