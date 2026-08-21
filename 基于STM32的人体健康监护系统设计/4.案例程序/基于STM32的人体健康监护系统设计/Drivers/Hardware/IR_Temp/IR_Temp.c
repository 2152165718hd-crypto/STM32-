#include ".\Hardware\IR_Temp\IR_Temp.h"

#define IR_TEMP_ACK 0U
#define IR_TEMP_NACK 1U
#define IR_TEMP_MAX_RETRY 3U

#if (IR_TEMP_USE_HARDWARE_I2C == 1U)
static I2C_HandleTypeDef s_hi2c1;
#else
static uint8_t s_dwt_ready = 0U;
#endif

static uint8_t IR_Temp_CalculatePEC(const uint8_t *data, uint8_t length)
{
    uint8_t crc = 0U;
    uint8_t index;
    uint8_t bit;

    for (index = 0U; index < length; index++)
    {
        crc ^= data[index];
        for (bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 0x80U) != 0U)
            {
                crc = (uint8_t)((crc << 1) ^ 0x07U);
            }
            else
            {
                crc <<= 1;
            }
        }
    }

    return crc;
}

#if (IR_TEMP_USE_HARDWARE_I2C == 1U)
void HAL_I2C_MspInit(I2C_HandleTypeDef *hi2c)
{
    GPIO_InitTypeDef gpio_init = {0};

    if (hi2c->Instance != I2C1)
    {
        return;
    }

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();

    gpio_init.Pin = IR_TEMP_SCL_PIN | IR_TEMP_SDA_PIN;
    gpio_init.Mode = GPIO_MODE_AF_OD;
    gpio_init.Pull = GPIO_PULLUP;
    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(IR_TEMP_SCL_PORT, &gpio_init);
}

static void IR_Temp_I2C_Init(void)
{
    s_hi2c1.Instance = I2C1;
    s_hi2c1.Init.ClockSpeed = 100000U;
    s_hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
    s_hi2c1.Init.OwnAddress1 = 0U;
    s_hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    s_hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    s_hi2c1.Init.OwnAddress2 = 0U;
    s_hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    s_hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

    (void)HAL_I2C_Init(&s_hi2c1);
}

static uint16_t IR_Temp_ReadMemoryHardware(uint8_t slave_address, uint8_t command)
{
    uint8_t buffer[3] = {0U, 0U, 0U};
    uint8_t frame[5];
    uint8_t pec_reg;
    uint8_t address = (uint8_t)(slave_address << 1);

    if (HAL_I2C_Mem_Read(&s_hi2c1,
                         address,
                         command,
                         I2C_MEMADD_SIZE_8BIT,
                         buffer,
                         3U,
                         100U) != HAL_OK)
    {
        return 0U;
    }

    frame[0] = address;
    frame[1] = command;
    frame[2] = (uint8_t)(address | 0x01U);
    frame[3] = buffer[0];
    frame[4] = buffer[1];
    pec_reg = IR_Temp_CalculatePEC(frame, 5U);

    if (pec_reg != buffer[2])
    {
        return 0U;
    }

    return (uint16_t)(((uint16_t)buffer[1] << 8) | buffer[0]);
}
#else
static void IR_Temp_EnableDwtDelay(void)
{
    if (s_dwt_ready != 0U)
    {
        return;
    }

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    s_dwt_ready = 1U;
}

static void IR_Temp_DelayUs(uint32_t us)
{
    uint32_t cycles_per_us = HAL_RCC_GetHCLKFreq() / 1000000U;
    uint32_t wait_cycles = cycles_per_us * us;
    uint32_t start = DWT->CYCCNT;

    while ((uint32_t)(DWT->CYCCNT - start) < wait_cycles)
    {
    }
}

static void IR_Temp_SCL_High(void)
{
    HAL_GPIO_WritePin(IR_TEMP_SCL_PORT, IR_TEMP_SCL_PIN, GPIO_PIN_SET);
}

static void IR_Temp_SCL_Low(void)
{
    HAL_GPIO_WritePin(IR_TEMP_SCL_PORT, IR_TEMP_SCL_PIN, GPIO_PIN_RESET);
}

static void IR_Temp_SDA_High(void)
{
    HAL_GPIO_WritePin(IR_TEMP_SDA_PORT, IR_TEMP_SDA_PIN, GPIO_PIN_SET);
}

static void IR_Temp_SDA_Low(void)
{
    HAL_GPIO_WritePin(IR_TEMP_SDA_PORT, IR_TEMP_SDA_PIN, GPIO_PIN_RESET);
}

static GPIO_PinState IR_Temp_SDA_Read(void)
{
    return HAL_GPIO_ReadPin(IR_TEMP_SDA_PORT, IR_TEMP_SDA_PIN);
}

static void IR_Temp_StartBit(void)
{
    IR_Temp_SDA_High();
    IR_Temp_DelayUs(5U);
    IR_Temp_SCL_High();
    IR_Temp_DelayUs(5U);
    IR_Temp_SDA_Low();
    IR_Temp_DelayUs(5U);
    IR_Temp_SCL_Low();
    IR_Temp_DelayUs(5U);
}

static void IR_Temp_StopBit(void)
{
    IR_Temp_SCL_Low();
    IR_Temp_DelayUs(5U);
    IR_Temp_SDA_Low();
    IR_Temp_DelayUs(5U);
    IR_Temp_SCL_High();
    IR_Temp_DelayUs(5U);
    IR_Temp_SDA_High();
    IR_Temp_DelayUs(5U);
}

static void IR_Temp_SendBit(uint8_t bit_out)
{
    if (bit_out == 0U)
    {
        IR_Temp_SDA_Low();
    }
    else
    {
        IR_Temp_SDA_High();
    }

    IR_Temp_DelayUs(2U);
    IR_Temp_SCL_High();
    IR_Temp_DelayUs(6U);
    IR_Temp_SCL_Low();
    IR_Temp_DelayUs(3U);
}

static uint8_t IR_Temp_ReceiveBit(void)
{
    uint8_t ack_bit;

    IR_Temp_SDA_High();
    IR_Temp_DelayUs(2U);
    IR_Temp_SCL_High();
    IR_Temp_DelayUs(5U);
    ack_bit = (IR_Temp_SDA_Read() == GPIO_PIN_SET) ? 1U : 0U;
    IR_Temp_SCL_Low();
    IR_Temp_DelayUs(3U);

    return ack_bit;
}

static uint8_t IR_Temp_SendByte(uint8_t tx_buffer)
{
    uint8_t bit_counter;

    for (bit_counter = 8U; bit_counter > 0U; bit_counter--)
    {
        IR_Temp_SendBit((uint8_t)((tx_buffer & 0x80U) ? 1U : 0U));
        tx_buffer <<= 1;
    }

    return IR_Temp_ReceiveBit();
}

static uint8_t IR_Temp_ReceiveByte(uint8_t ack_nack)
{
    uint8_t rx_buffer = 0U;
    uint8_t bit_counter;

    for (bit_counter = 8U; bit_counter > 0U; bit_counter--)
    {
        rx_buffer <<= 1;
        if (IR_Temp_ReceiveBit() != 0U)
        {
            rx_buffer |= 0x01U;
        }
    }

    IR_Temp_SendBit(ack_nack);
    return rx_buffer;
}

static uint16_t IR_Temp_ReadMemorySoftware(uint8_t slave_address, uint8_t command)
{
    uint16_t data = 0U;
    uint8_t data_l = 0U;
    uint8_t data_h = 0U;
    uint8_t pec = 0U;
    uint8_t pec_reg = 0U;
    uint8_t frame[5];
    uint8_t attempt;
    uint8_t address = (uint8_t)(slave_address << 1);

    for (attempt = 0U; attempt < IR_TEMP_MAX_RETRY; attempt++)
    {
        IR_Temp_StopBit();

        IR_Temp_StartBit();
        if (IR_Temp_SendByte(address) != 0U)
        {
            continue;
        }

        if (IR_Temp_SendByte(command) != 0U)
        {
            continue;
        }

        IR_Temp_StartBit();
        if (IR_Temp_SendByte((uint8_t)(address | 0x01U)) != 0U)
        {
            continue;
        }

        data_l = IR_Temp_ReceiveByte(IR_TEMP_ACK);
        data_h = IR_Temp_ReceiveByte(IR_TEMP_ACK);
        pec = IR_Temp_ReceiveByte(IR_TEMP_NACK);
        IR_Temp_StopBit();

        frame[0] = address;
        frame[1] = command;
        frame[2] = (uint8_t)(address | 0x01U);
        frame[3] = data_l;
        frame[4] = data_h;
        pec_reg = IR_Temp_CalculatePEC(frame, 5U);

        if (pec_reg == pec)
        {
            data = (uint16_t)(((uint16_t)data_h << 8) | data_l);
            return data;
        }
    }

    return 0U;
}
#endif

void IR_Temp_Init(void)
{
#if (IR_TEMP_USE_HARDWARE_I2C == 1U)
    IR_Temp_I2C_Init();
#else
    GPIO_InitTypeDef gpio_init = {0};

    IR_Temp_EnableDwtDelay();

    __HAL_RCC_GPIOB_CLK_ENABLE();

    gpio_init.Pin = IR_TEMP_SCL_PIN | IR_TEMP_SDA_PIN;
    gpio_init.Mode = GPIO_MODE_OUTPUT_OD;
    gpio_init.Pull = GPIO_PULLUP;
    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(IR_TEMP_SCL_PORT, &gpio_init);

    IR_Temp_SCL_High();
    IR_Temp_SDA_High();
#endif
}

float IR_Temp_ReadObjectCelsius(void)
{
    uint16_t raw;

#if (IR_TEMP_USE_HARDWARE_I2C == 1U)
    raw = IR_Temp_ReadMemoryHardware(IR_TEMP_MLX90614_ADDR, (uint8_t)(IR_TEMP_RAM_ACCESS | IR_TEMP_RAM_TOBJ1));
#else
    raw = IR_Temp_ReadMemorySoftware(IR_TEMP_MLX90614_ADDR, (uint8_t)(IR_TEMP_RAM_ACCESS | IR_TEMP_RAM_TOBJ1));
#endif

    if (raw == 0U)
    {
        return IR_TEMP_INVALID_CELSIUS;
    }

    return ((float)raw * 0.02f) - 273.15f;
}

void SMBus_Init(void)
{
    IR_Temp_Init();
}

float SMBus_ReadTemp(void)
{
    return IR_Temp_ReadObjectCelsius();
}
