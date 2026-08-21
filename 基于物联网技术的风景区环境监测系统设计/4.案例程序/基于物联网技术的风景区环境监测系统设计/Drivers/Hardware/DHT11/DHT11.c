#include ".\Hardware\DHT11\DHT11.h"

#define DHT11_START_TIMEOUT_US 100U
#define DHT11_BIT_TIMEOUT_US 100U

DHT11_Data_t DHT11_Data;

static void DHT11_GPIO_Clock_Enable(void)
{
    if (DHT11_Data_PORT == GPIOA)
    {
        __HAL_RCC_GPIOA_CLK_ENABLE();
    }
    else if (DHT11_Data_PORT == GPIOB)
    {
        __HAL_RCC_GPIOB_CLK_ENABLE();
    }
    else if (DHT11_Data_PORT == GPIOC)
    {
        __HAL_RCC_GPIOC_CLK_ENABLE();
    }
#if defined(GPIOD)
    else if (DHT11_Data_PORT == GPIOD)
    {
        __HAL_RCC_GPIOD_CLK_ENABLE();
    }
#endif
#if defined(GPIOE)
    else if (DHT11_Data_PORT == GPIOE)
    {
        __HAL_RCC_GPIOE_CLK_ENABLE();
    }
#endif
}

static void DHT11_PIN_OUT(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Pin = DHT11_Data_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(DHT11_Data_PORT, &GPIO_InitStruct);
}

static void DHT11_PIN_IN(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Pin = DHT11_Data_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(DHT11_Data_PORT, &GPIO_InitStruct);
}

static void DHT11_DATA_Write(uint8_t state)
{
    HAL_GPIO_WritePin(DHT11_Data_PORT,
                      DHT11_Data_PIN,
                      state ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static uint8_t DHT11_DATA_Read(void)
{
    return (uint8_t)HAL_GPIO_ReadPin(DHT11_Data_PORT, DHT11_Data_PIN);
}

static void DHT11_RestoreIRQ(uint32_t primask)
{
    if ((primask & 1U) == 0U)
    {
        __enable_irq();
    }
}

static void DHT11_ReleaseBus(void)
{
    DHT11_PIN_OUT();
    DHT11_DATA_Write(1U);
}

static uint8_t DHT11_Start(void)
{
    uint16_t retry;

    DHT11_PIN_OUT();
    DHT11_DATA_Write(0U);
    delay_ms(20U);

    DHT11_DATA_Write(1U);
    delay_us(30U);
    DHT11_PIN_IN();

    retry = 0U;
    while ((DHT11_DATA_Read() != 0U) && (retry < DHT11_START_TIMEOUT_US))
    {
        retry++;
        delay_us(1U);
    }
    if (retry >= DHT11_START_TIMEOUT_US)
    {
        return DHT11_STATUS_NO_RESPONSE;
    }

    retry = 0U;
    while ((DHT11_DATA_Read() == 0U) && (retry < DHT11_START_TIMEOUT_US))
    {
        retry++;
        delay_us(1U);
    }
    if (retry >= DHT11_START_TIMEOUT_US)
    {
        return DHT11_STATUS_NO_RESPONSE;
    }

    retry = 0U;
    while ((DHT11_DATA_Read() != 0U) && (retry < DHT11_START_TIMEOUT_US))
    {
        retry++;
        delay_us(1U);
    }
    if (retry >= DHT11_START_TIMEOUT_US)
    {
        return DHT11_STATUS_NO_RESPONSE;
    }

    return DHT11_STATUS_OK;
}

static uint8_t DHT11_ReadByte(uint8_t *byte)
{
    uint8_t i;
    uint8_t value = 0U;
    uint16_t retry;

    if (byte == NULL)
    {
        return DHT11_STATUS_TIMEOUT;
    }

    for (i = 0U; i < 8U; i++)
    {
        retry = 0U;
        while ((DHT11_DATA_Read() == 0U) && (retry < DHT11_BIT_TIMEOUT_US))
        {
            retry++;
            delay_us(1U);
        }
        if (retry >= DHT11_BIT_TIMEOUT_US)
        {
            return DHT11_STATUS_TIMEOUT;
        }

        delay_us(40U);
        value <<= 1;

        if (DHT11_DATA_Read() != 0U)
        {
            value |= 0x01U;

            retry = 0U;
            while ((DHT11_DATA_Read() != 0U) && (retry < DHT11_BIT_TIMEOUT_US))
            {
                retry++;
                delay_us(1U);
            }
            if (retry >= DHT11_BIT_TIMEOUT_US)
            {
                return DHT11_STATUS_TIMEOUT;
            }
        }
    }

    *byte = value;
    return DHT11_STATUS_OK;
}

uint8_t DHT11_Init(void)
{
    DHT11_GPIO_Clock_Enable();
    DHT11_ReleaseBus();
    delay_ms(1000U);

    return DHT11_Start();
}

uint8_t DHT11_ReadData(void)
{
    uint8_t buf[5] = {0};
    uint8_t i;
    uint8_t status;
    uint32_t primask;

    status = DHT11_Start();
    if (status != DHT11_STATUS_OK)
    {
        DHT11_ReleaseBus();
        return status;
    }

    primask = __get_PRIMASK();
    __disable_irq();

    for (i = 0U; i < 5U; i++)
    {
        status = DHT11_ReadByte(&buf[i]);
        if (status != DHT11_STATUS_OK)
        {
            DHT11_RestoreIRQ(primask);
            DHT11_ReleaseBus();
            return status;
        }
    }

    DHT11_RestoreIRQ(primask);
    DHT11_ReleaseBus();

    if ((uint8_t)(buf[0] + buf[1] + buf[2] + buf[3]) != buf[4])
    {
        return DHT11_STATUS_CHECKSUM_ERROR;
    }

    if ((buf[0] > 100U) || (buf[2] > 100U))
    {
        return DHT11_STATUS_RANGE_ERROR;
    }

    DHT11_Data.humi_int = buf[0];
    DHT11_Data.temp_int = buf[2];

    return DHT11_STATUS_OK;
}

uint8_t DHT11_GetTemperature(void)
{
    (void)DHT11_ReadData();
    return DHT11_Data.temp_int;
}

uint8_t DHT11_GetHumidity(void)
{
    (void)DHT11_ReadData();
    return DHT11_Data.humi_int;
}

uint8_t DHT11_Read(uint8_t *humi_int, uint8_t *humi_dec, uint8_t *temp_int, uint8_t *temp_dec)
{
    if (DHT11_ReadData() != DHT11_STATUS_OK)
    {
        return 0U;
    }

    if (humi_int != NULL)
    {
        *humi_int = DHT11_Data.humi_int;
    }
    if (humi_dec != NULL)
    {
        *humi_dec = 0U;
    }
    if (temp_int != NULL)
    {
        *temp_int = DHT11_Data.temp_int;
    }
    if (temp_dec != NULL)
    {
        *temp_dec = 0U;
    }

    return 1U;
}
