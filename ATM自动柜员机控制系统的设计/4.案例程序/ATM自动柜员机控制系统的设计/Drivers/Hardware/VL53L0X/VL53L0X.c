#include ".\Hardware\VL53L0X\VL53L0X.h"

#define VL53L0X_I2C_ADDR                        (0x29u << 1)
#define VL53L0X_REG_SYSRANGE_START              0x00u
#define VL53L0X_REG_SYSTEM_INTERRUPT_CONFIG_GPIO 0x0Au
#define VL53L0X_REG_SYSTEM_INTERRUPT_CLEAR      0x0Bu
#define VL53L0X_REG_RESULT_INTERRUPT_STATUS     0x13u
#define VL53L0X_REG_RESULT_RANGE_STATUS         0x14u
#define VL53L0X_REG_GPIO_HV_MUX_ACTIVE_HIGH     0x84u
#define VL53L0X_REG_MODEL_ID                    0xC0u
#define VL53L0X_TIMEOUT_MS                      50u
#define VL53L0X_I2C_DELAY_CYCLES                200u
#define VL53L0X_GPIO_HV_ACTIVE_HIGH_BIT         0x10u
#define VL53L0X_GPIO1_READY_MASK                0x07u
#define VL53L0X_XSHUT_RESET_DELAY_MS            2u
#define VL53L0X_BOOT_DELAY_MS                   10u

static uint8_t s_vl53_ready = 0u;
static uint8_t s_stop_variable = 0u;

static void VL53L0X_I2C_Delay(void)
{
    volatile uint32_t i;
    for (i = 0u; i < VL53L0X_I2C_DELAY_CYCLES; ++i)
    {
        __NOP();
    }
}

static void VL53L0X_GPIO_EnableClock(GPIO_TypeDef *port)
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
#ifdef GPIOE
    else if (port == GPIOE)
    {
        __HAL_RCC_GPIOE_CLK_ENABLE();
    }
#endif
}

static void VL53L0X_SDA_Output(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Pin = VL53L0X_SDA_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(VL53L0X_SDA_PORT, &GPIO_InitStruct);
}

static void VL53L0X_SDA_Input(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Pin = VL53L0X_SDA_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(VL53L0X_SDA_PORT, &GPIO_InitStruct);
}

static void VL53L0X_I2C_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    VL53L0X_GPIO_EnableClock(VL53L0X_SCL_PORT);
    if (VL53L0X_SDA_PORT != VL53L0X_SCL_PORT)
    {
        VL53L0X_GPIO_EnableClock(VL53L0X_SDA_PORT);
    }

    GPIO_InitStruct.Pin = VL53L0X_SCL_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(VL53L0X_SCL_PORT, &GPIO_InitStruct);

    VL53L0X_SDA_Output();
    HAL_GPIO_WritePin(VL53L0X_SCL_PORT, VL53L0X_SCL_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(VL53L0X_SDA_PORT, VL53L0X_SDA_PIN, GPIO_PIN_SET);
}

static void VL53L0X_IO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    VL53L0X_I2C_Init();

    VL53L0X_GPIO_EnableClock(VL53L0X_XSHUT_PORT);
    if (VL53L0X_GPIO1_PORT != VL53L0X_XSHUT_PORT)
    {
        VL53L0X_GPIO_EnableClock(VL53L0X_GPIO1_PORT);
    }

    GPIO_InitStruct.Pin = VL53L0X_XSHUT_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(VL53L0X_XSHUT_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = VL53L0X_GPIO1_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(VL53L0X_GPIO1_PORT, &GPIO_InitStruct);

    HAL_GPIO_WritePin(VL53L0X_XSHUT_PORT, VL53L0X_XSHUT_PIN, GPIO_PIN_SET);
}

static void VL53L0X_SCL_High(void)
{
    HAL_GPIO_WritePin(VL53L0X_SCL_PORT, VL53L0X_SCL_PIN, GPIO_PIN_SET);
}

static void VL53L0X_SCL_Low(void)
{
    HAL_GPIO_WritePin(VL53L0X_SCL_PORT, VL53L0X_SCL_PIN, GPIO_PIN_RESET);
}

static void VL53L0X_SDA_High(void)
{
    HAL_GPIO_WritePin(VL53L0X_SDA_PORT, VL53L0X_SDA_PIN, GPIO_PIN_SET);
}

static void VL53L0X_SDA_Low(void)
{
    HAL_GPIO_WritePin(VL53L0X_SDA_PORT, VL53L0X_SDA_PIN, GPIO_PIN_RESET);
}

static uint8_t VL53L0X_SDA_Read(void)
{
    return (HAL_GPIO_ReadPin(VL53L0X_SDA_PORT, VL53L0X_SDA_PIN) == GPIO_PIN_SET) ? 1u : 0u;
}

void VL53L0X_SetXshut(uint8_t level)
{
    HAL_GPIO_WritePin(VL53L0X_XSHUT_PORT, VL53L0X_XSHUT_PIN, (level != 0u) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

uint8_t VL53L0X_GetXshut(void)
{
    return (HAL_GPIO_ReadPin(VL53L0X_XSHUT_PORT, VL53L0X_XSHUT_PIN) == GPIO_PIN_SET) ? 1u : 0u;
}

uint8_t VL53L0X_Gpio1_IsActive(void)
{
    GPIO_PinState state = HAL_GPIO_ReadPin(VL53L0X_GPIO1_PORT, VL53L0X_GPIO1_PIN);

    if (VL53L0X_GPIO1_ACTIVE_HIGH != 0u)
    {
        return (state == GPIO_PIN_SET) ? 1u : 0u;
    }

    return (state == GPIO_PIN_RESET) ? 1u : 0u;
}

static void VL53L0X_I2C_Start(void)
{
    VL53L0X_SDA_Output();
    VL53L0X_SDA_High();
    VL53L0X_SCL_High();
    VL53L0X_I2C_Delay();
    VL53L0X_SDA_Low();
    VL53L0X_I2C_Delay();
    VL53L0X_SCL_Low();
    VL53L0X_I2C_Delay();
}

static void VL53L0X_I2C_Stop(void)
{
    VL53L0X_SDA_Output();
    VL53L0X_SDA_Low();
    VL53L0X_I2C_Delay();
    VL53L0X_SCL_High();
    VL53L0X_I2C_Delay();
    VL53L0X_SDA_High();
    VL53L0X_I2C_Delay();
}

static uint8_t VL53L0X_I2C_WriteByte(uint8_t data)
{
    uint8_t bit;
    uint8_t ack;

    VL53L0X_SDA_Output();
    for (bit = 0u; bit < 8u; ++bit)
    {
        if ((data & 0x80u) != 0u)
        {
            VL53L0X_SDA_High();
        }
        else
        {
            VL53L0X_SDA_Low();
        }
        VL53L0X_I2C_Delay();
        VL53L0X_SCL_High();
        VL53L0X_I2C_Delay();
        VL53L0X_SCL_Low();
        VL53L0X_I2C_Delay();
        data <<= 1;
    }

    VL53L0X_SDA_Input();
    VL53L0X_I2C_Delay();
    VL53L0X_SCL_High();
    VL53L0X_I2C_Delay();
    ack = VL53L0X_SDA_Read();
    VL53L0X_SCL_Low();
    VL53L0X_SDA_Output();

    return (ack == 0u) ? 1u : 0u;
}

static uint8_t VL53L0X_I2C_ReadByte(uint8_t ack)
{
    uint8_t bit;
    uint8_t data = 0u;

    VL53L0X_SDA_Input();
    for (bit = 0u; bit < 8u; ++bit)
    {
        VL53L0X_SCL_High();
        VL53L0X_I2C_Delay();
        data = (uint8_t)(data << 1);
        if (VL53L0X_SDA_Read() != 0u)
        {
            data |= 0x01u;
        }
        VL53L0X_SCL_Low();
        VL53L0X_I2C_Delay();
    }

    VL53L0X_SDA_Output();
    if (ack != 0u)
    {
        VL53L0X_SDA_Low();
    }
    else
    {
        VL53L0X_SDA_High();
    }
    VL53L0X_I2C_Delay();
    VL53L0X_SCL_High();
    VL53L0X_I2C_Delay();
    VL53L0X_SCL_Low();
    VL53L0X_SDA_High();

    return data;
}

static uint8_t VL53L0X_WriteReg8(uint8_t reg, uint8_t value)
{
    VL53L0X_I2C_Start();
    if (VL53L0X_I2C_WriteByte((uint8_t)(VL53L0X_I2C_ADDR | 0x00u)) == 0u)
    {
        VL53L0X_I2C_Stop();
        return 0u;
    }
    if (VL53L0X_I2C_WriteByte(reg) == 0u)
    {
        VL53L0X_I2C_Stop();
        return 0u;
    }
    if (VL53L0X_I2C_WriteByte(value) == 0u)
    {
        VL53L0X_I2C_Stop();
        return 0u;
    }
    VL53L0X_I2C_Stop();
    return 1u;
}

static uint8_t VL53L0X_ReadReg8(uint8_t reg, uint8_t *value)
{
    VL53L0X_I2C_Start();
    if (VL53L0X_I2C_WriteByte((uint8_t)(VL53L0X_I2C_ADDR | 0x00u)) == 0u)
    {
        VL53L0X_I2C_Stop();
        return 0u;
    }
    if (VL53L0X_I2C_WriteByte(reg) == 0u)
    {
        VL53L0X_I2C_Stop();
        return 0u;
    }
    VL53L0X_I2C_Start();
    if (VL53L0X_I2C_WriteByte((uint8_t)(VL53L0X_I2C_ADDR | 0x01u)) == 0u)
    {
        VL53L0X_I2C_Stop();
        return 0u;
    }
    *value = VL53L0X_I2C_ReadByte(0u);
    VL53L0X_I2C_Stop();
    return 1u;
}

static uint8_t VL53L0X_ReadMulti(uint8_t reg, uint8_t *buf, uint16_t len)
{
    uint16_t i;

    VL53L0X_I2C_Start();
    if (VL53L0X_I2C_WriteByte((uint8_t)(VL53L0X_I2C_ADDR | 0x00u)) == 0u)
    {
        VL53L0X_I2C_Stop();
        return 0u;
    }
    if (VL53L0X_I2C_WriteByte(reg) == 0u)
    {
        VL53L0X_I2C_Stop();
        return 0u;
    }
    VL53L0X_I2C_Start();
    if (VL53L0X_I2C_WriteByte((uint8_t)(VL53L0X_I2C_ADDR | 0x01u)) == 0u)
    {
        VL53L0X_I2C_Stop();
        return 0u;
    }

    for (i = 0u; i < len; ++i)
    {
        uint8_t ack = (i + 1u < len) ? 1u : 0u;
        buf[i] = VL53L0X_I2C_ReadByte(ack);
    }
    VL53L0X_I2C_Stop();
    return 1u;
}

static uint8_t VL53L0X_ClearInterrupt(void)
{
    return VL53L0X_WriteReg8(VL53L0X_REG_SYSTEM_INTERRUPT_CLEAR, 0x01u);
}

static uint8_t VL53L0X_ConfigureGpio1(void)
{
    uint8_t reg_value = 0u;

    if (VL53L0X_WriteReg8(VL53L0X_REG_SYSTEM_INTERRUPT_CONFIG_GPIO, 0x04u) == 0u)
    {
        return 0u;
    }

    if (VL53L0X_ReadReg8(VL53L0X_REG_GPIO_HV_MUX_ACTIVE_HIGH, &reg_value) == 0u)
    {
        return 0u;
    }

    if (VL53L0X_GPIO1_ACTIVE_HIGH != 0u)
    {
        reg_value |= VL53L0X_GPIO_HV_ACTIVE_HIGH_BIT;
    }
    else
    {
        reg_value &= (uint8_t)~VL53L0X_GPIO_HV_ACTIVE_HIGH_BIT;
    }

    if (VL53L0X_WriteReg8(VL53L0X_REG_GPIO_HV_MUX_ACTIVE_HIGH, reg_value) == 0u)
    {
        return 0u;
    }

    return VL53L0X_ClearInterrupt();
}

static uint8_t VL53L0X_IsDataReady(void)
{
    uint8_t status = 0u;

    if (VL53L0X_Gpio1_IsActive() != 0u)
    {
        return 1u;
    }

    if (VL53L0X_ReadReg8(VL53L0X_REG_RESULT_INTERRUPT_STATUS, &status) == 0u)
    {
        return 0u;
    }

    return ((status & VL53L0X_GPIO1_READY_MASK) != 0u) ? 1u : 0u;
}

static uint8_t VL53L0X_WaitForDataReady(uint32_t timeout_ms)
{
    uint32_t start_tick = HAL_GetTick();

    do
    {
        if (VL53L0X_IsDataReady() != 0u)
        {
            return 1u;
        }
        HAL_Delay(1u);
    } while ((HAL_GetTick() - start_tick) < timeout_ms);

    return 0u;
}

static uint8_t VL53L0X_PrepareSingleShot(void)
{
    if (VL53L0X_WriteReg8(0x80u, 0x01u) == 0u)
    {
        return 0u;
    }
    if (VL53L0X_WriteReg8(0xFFu, 0x01u) == 0u)
    {
        return 0u;
    }
    if (VL53L0X_WriteReg8(0x00u, 0x00u) == 0u)
    {
        return 0u;
    }
    if (VL53L0X_WriteReg8(0x91u, s_stop_variable) == 0u)
    {
        return 0u;
    }
    if (VL53L0X_WriteReg8(0x00u, 0x01u) == 0u)
    {
        return 0u;
    }
    if (VL53L0X_WriteReg8(0xFFu, 0x00u) == 0u)
    {
        return 0u;
    }
    if (VL53L0X_WriteReg8(0x80u, 0x00u) == 0u)
    {
        return 0u;
    }

    return 1u;
}

static uint8_t VL53L0X_StartMeasurement(void)
{
    if (VL53L0X_PrepareSingleShot() == 0u)
    {
        return 0u;
    }

    return VL53L0X_WriteReg8(VL53L0X_REG_SYSRANGE_START, 0x01u);
}

uint8_t VL53L0X_Init(void)
{
    uint8_t model_id = 0u;
    uint8_t reg_value = 0u;

    VL53L0X_IO_Init();
    VL53L0X_SetXshut(0u);
    HAL_Delay(VL53L0X_XSHUT_RESET_DELAY_MS);
    VL53L0X_SetXshut(1u);
    HAL_Delay(VL53L0X_BOOT_DELAY_MS);

    if (VL53L0X_ReadReg8(VL53L0X_REG_MODEL_ID, &model_id) == 0u)
    {
        s_vl53_ready = 0u;
        return 0u;
    }

    if (model_id != 0xEEu)
    {
        s_vl53_ready = 0u;
        return 0u;
    }

    if (VL53L0X_ReadReg8(0x89u, &reg_value) != 0u)
    {
        (void)VL53L0X_WriteReg8(0x89u, (uint8_t)(reg_value | 0x01u));
    }

    (void)VL53L0X_WriteReg8(0x88u, 0x00u);
    (void)VL53L0X_WriteReg8(0x80u, 0x01u);
    (void)VL53L0X_WriteReg8(0xFFu, 0x01u);
    (void)VL53L0X_WriteReg8(0x00u, 0x00u);
    (void)VL53L0X_ReadReg8(0x91u, &s_stop_variable);
    (void)VL53L0X_WriteReg8(0x00u, 0x01u);
    (void)VL53L0X_WriteReg8(0xFFu, 0x00u);
    (void)VL53L0X_WriteReg8(0x80u, 0x00u);

    if (VL53L0X_ReadReg8(0x60u, &reg_value) != 0u)
    {
        (void)VL53L0X_WriteReg8(0x60u, (uint8_t)(reg_value | 0x12u));
    }

    if (VL53L0X_ConfigureGpio1() == 0u)
    {
        s_vl53_ready = 0u;
        return 0u;
    }

    s_vl53_ready = 1u;
    return 1u;
}

uint8_t VL53L0X_IsReady(void)
{
    return s_vl53_ready;
}

uint16_t VL53L0X_ReadDistanceMm(void)
{
    uint8_t buf[12];

    if (s_vl53_ready == 0u)
    {
        if (VL53L0X_Init() == 0u)
        {
            return VL53L0X_DISTANCE_INVALID;
        }
    }

    if (VL53L0X_StartMeasurement() == 0u)
    {
        s_vl53_ready = 0u;
        return VL53L0X_DISTANCE_INVALID;
    }

    if (VL53L0X_WaitForDataReady(VL53L0X_TIMEOUT_MS) == 0u)
    {
        return VL53L0X_DISTANCE_INVALID;
    }

    if (VL53L0X_ReadMulti(VL53L0X_REG_RESULT_RANGE_STATUS, buf, sizeof(buf)) == 0u)
    {
        s_vl53_ready = 0u;
        return VL53L0X_DISTANCE_INVALID;
    }

    (void)VL53L0X_ClearInterrupt();

    return (uint16_t)(((uint16_t)buf[10] << 8) | buf[11]);
}

uint8_t VL53L0X_IsBlocked(uint16_t threshold_mm)
{
    uint16_t distance = VL53L0X_ReadDistanceMm();

    if (distance == VL53L0X_DISTANCE_INVALID)
    {
        return 0u;
    }

    return (distance < threshold_mm) ? 1u : 0u;
}
