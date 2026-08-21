#include "Hardware/INA226/INA226.h"

#define INA226_I2C_INSTANCE           I2C2
#define INA226_I2C_GPIO_PORT          GPIOB
#define INA226_I2C_SCL_PIN            GPIO_PIN_10
#define INA226_I2C_SDA_PIN            GPIO_PIN_11
#define INA226_ALERT_GPIO_PORT        GPIOB
#define INA226_ALERT_GPIO_PIN         GPIO_PIN_1
#define INA226_SCAN_ADDR_START        0x40U
#define INA226_SCAN_ADDR_END          0x4FU

static I2C_HandleTypeDef g_ina_i2c;
static INA226_Handle_t g_ina_device;
static INA226_Diag_t g_ina_diag;
static uint16_t g_ina_calibration = 0U;

static int32_t INA226_RoundFloatToInt32(float value)
{
    if (value >= 0.0f)
    {
        return (int32_t)(value + 0.5f);
    }

    return (int32_t)(value - 0.5f);
}

static void INA226_RecordFailure(INA226_DiagStage_t stage, HAL_StatusTypeDef hal_status)
{
    g_ina_diag.ready = g_ina_device.is_ready;
    g_ina_diag.last_stage = (uint8_t)stage;
    g_ina_diag.last_hal_status = (uint8_t)hal_status;
    g_ina_diag.last_hal_error_code = HAL_I2C_ERROR_NONE;
    if ((g_ina_device.hi2c != NULL) && (hal_status != HAL_OK))
    {
        g_ina_diag.last_hal_error_code = HAL_I2C_GetError(g_ina_device.hi2c);
    }
}

static void INA226_RecordDriverFailure(INA226_DiagStage_t stage, INA226_Status_t status)
{
    if (status == INA226_ERR_I2C)
    {
        INA226_RecordFailure(stage, HAL_ERROR);
        return;
    }

    INA226_RecordFailure(stage, HAL_ERROR);
    g_ina_diag.last_hal_error_code = HAL_I2C_ERROR_NONE;
}

static void INA226_RecordLogicFailure(INA226_DiagStage_t stage)
{
    INA226_RecordFailure(stage, HAL_ERROR);
    g_ina_diag.last_hal_error_code = HAL_I2C_ERROR_NONE;
}

static void INA226_RecordSuccess(void)
{
    g_ina_diag.ready = g_ina_device.is_ready;
    g_ina_diag.detected_address_7bit = (uint8_t)(g_ina_device.i2c_addr >> 1);
    g_ina_diag.last_stage = (uint8_t)INA226_DIAG_STAGE_NONE;
    g_ina_diag.last_hal_status = (uint8_t)HAL_OK;
    g_ina_diag.last_hal_error_code = HAL_I2C_ERROR_NONE;
}

static INA226_Status_t INA226_WriteReg16(INA226_Handle_t *dev, uint8_t reg, uint16_t value)
{
    uint8_t tx[2];

    if ((dev == NULL) || (dev->hi2c == NULL))
    {
        return INA226_ERR_PARAM;
    }

    tx[0] = (uint8_t)((value >> 8) & 0xFFU);
    tx[1] = (uint8_t)(value & 0xFFU);

    if (HAL_I2C_Mem_Write(dev->hi2c,
                          dev->i2c_addr,
                          reg,
                          I2C_MEMADD_SIZE_8BIT,
                          tx,
                          2U,
                          INA226_I2C_TIMEOUT_MS) != HAL_OK)
    {
        return INA226_ERR_I2C;
    }

    return INA226_OK;
}

static INA226_Status_t INA226_ReadReg16(INA226_Handle_t *dev, uint8_t reg, uint16_t *value)
{
    uint8_t rx[2];

    if ((dev == NULL) || (dev->hi2c == NULL) || (value == NULL))
    {
        return INA226_ERR_PARAM;
    }

    if (HAL_I2C_Mem_Read(dev->hi2c,
                         dev->i2c_addr,
                         reg,
                         I2C_MEMADD_SIZE_8BIT,
                         rx,
                         2U,
                         INA226_I2C_TIMEOUT_MS) != HAL_OK)
    {
        return INA226_ERR_I2C;
    }

    *value = (uint16_t)((((uint16_t)rx[0]) << 8) | rx[1]);
    return INA226_OK;
}

static INA226_Status_t INA226_PrepareDevice(INA226_Handle_t *dev,
                                            I2C_HandleTypeDef *hi2c,
                                            uint16_t i2c_addr,
                                            float shunt_ohm,
                                            float current_lsb_a,
                                            uint16_t *calibration)
{
    float calibration_f;

    if ((dev == NULL) || (hi2c == NULL) || (shunt_ohm <= 0.0f) || (current_lsb_a <= 0.0f) || (calibration == NULL))
    {
        return INA226_ERR_PARAM;
    }

    dev->hi2c = hi2c;
    dev->i2c_addr = i2c_addr;
    dev->shunt_ohm = shunt_ohm;
    dev->current_lsb_a = current_lsb_a;
    dev->power_lsb_w = 25.0f * current_lsb_a;
    dev->is_ready = 0U;

    calibration_f = 0.00512f / (current_lsb_a * shunt_ohm);
    if ((calibration_f < 1.0f) || (calibration_f > 65535.0f))
    {
        return INA226_ERR_PARAM;
    }

    *calibration = (uint16_t)(calibration_f + 0.5f);
    return INA226_OK;
}

static HAL_StatusTypeDef INA226_I2C2_Init(void)
{
    GPIO_InitTypeDef gpio_init = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_RCC_I2C2_CLK_ENABLE();

    gpio_init.Pin = INA226_I2C_SCL_PIN | INA226_I2C_SDA_PIN;
    gpio_init.Mode = GPIO_MODE_AF_OD;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(INA226_I2C_GPIO_PORT, &gpio_init);

    gpio_init.Pin = INA226_ALERT_GPIO_PIN;
    gpio_init.Mode = GPIO_MODE_INPUT;
    gpio_init.Pull = GPIO_PULLUP;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(INA226_ALERT_GPIO_PORT, &gpio_init);

    __HAL_RCC_I2C2_FORCE_RESET();
    __HAL_RCC_I2C2_RELEASE_RESET();

    g_ina_i2c.Instance = INA226_I2C_INSTANCE;
    g_ina_i2c.Init.ClockSpeed = 100000U;
    g_ina_i2c.Init.DutyCycle = I2C_DUTYCYCLE_2;
    g_ina_i2c.Init.OwnAddress1 = 0U;
    g_ina_i2c.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    g_ina_i2c.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    g_ina_i2c.Init.OwnAddress2 = 0U;
    g_ina_i2c.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    g_ina_i2c.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

    if (HAL_I2C_Init(&g_ina_i2c) != HAL_OK)
    {
        g_ina_device.hi2c = &g_ina_i2c;
        INA226_RecordFailure(INA226_DIAG_STAGE_I2C_INIT, HAL_ERROR);
        return HAL_ERROR;
    }

    return HAL_OK;
}

static HAL_StatusTypeDef INA226_ScanAddress(uint8_t *detected_address_7bit)
{
    uint8_t addr;
    HAL_StatusTypeDef last_status = HAL_ERROR;

    if (detected_address_7bit == NULL)
    {
        INA226_RecordFailure(INA226_DIAG_STAGE_SCAN_ADDRESS, HAL_ERROR);
        return HAL_ERROR;
    }

    for (addr = INA226_SCAN_ADDR_START; addr <= INA226_SCAN_ADDR_END; addr++)
    {
        last_status = HAL_I2C_IsDeviceReady(&g_ina_i2c,
                                            (uint16_t)(addr << 1),
                                            2U,
                                            INA226_I2C_TIMEOUT_MS);
        if (last_status == HAL_OK)
        {
            *detected_address_7bit = addr;
            g_ina_diag.detected_address_7bit = addr;
            return HAL_OK;
        }
    }

    g_ina_diag.detected_address_7bit = 0U;
    INA226_RecordFailure(INA226_DIAG_STAGE_SCAN_ADDRESS, last_status);
    return HAL_ERROR;
}

static HAL_StatusTypeDef INA226_EnsureCalibration(void)
{
    uint16_t calibration_reg = 0U;
    INA226_Status_t status;

    if (g_ina_device.is_ready == 0U)
    {
        INA226_RecordDriverFailure(INA226_DIAG_STAGE_ENSURE_CALIB_READ, INA226_ERR_NOT_READY);
        return HAL_ERROR;
    }

    status = INA226_ReadReg16(&g_ina_device, INA226_REG_CALIBRATION, &calibration_reg);
    if (status != INA226_OK)
    {
        INA226_RecordDriverFailure(INA226_DIAG_STAGE_ENSURE_CALIB_READ, status);
        return HAL_ERROR;
    }

    if (calibration_reg == g_ina_calibration)
    {
        return HAL_OK;
    }

    status = INA226_WriteReg16(&g_ina_device, INA226_REG_CALIBRATION, g_ina_calibration);
    if (status != INA226_OK)
    {
        INA226_RecordDriverFailure(INA226_DIAG_STAGE_ENSURE_CALIB_WRITE, status);
        return HAL_ERROR;
    }

    return HAL_OK;
}

INA226_Status_t INA226_DeviceInit(INA226_Handle_t *dev,
                                  I2C_HandleTypeDef *hi2c,
                                  uint16_t i2c_addr,
                                  float shunt_ohm,
                                  float current_lsb_a)
{
    uint16_t calibration = 0U;
    INA226_Status_t status;

    status = INA226_PrepareDevice(dev, hi2c, i2c_addr, shunt_ohm, current_lsb_a, &calibration);
    if (status != INA226_OK)
    {
        return status;
    }

    status = INA226_WriteReg16(dev, INA226_REG_CONFIG, INA226_CONFIG_DEFAULT);
    if (status != INA226_OK)
    {
        return status;
    }

    status = INA226_WriteReg16(dev, INA226_REG_CALIBRATION, calibration);
    if (status != INA226_OK)
    {
        return status;
    }

    dev->is_ready = 1U;
    return INA226_OK;
}

INA226_Status_t INA226_DeviceReadBusVoltage_V(INA226_Handle_t *dev, float *voltage_v)
{
    uint16_t raw = 0U;
    INA226_Status_t status;

    if ((dev == NULL) || (voltage_v == NULL))
    {
        return INA226_ERR_PARAM;
    }

    if (dev->is_ready == 0U)
    {
        return INA226_ERR_NOT_READY;
    }

    status = INA226_ReadReg16(dev, INA226_REG_BUS_VOLTAGE, &raw);
    if (status != INA226_OK)
    {
        return status;
    }

    *voltage_v = (float)raw * 0.00125f;
    return INA226_OK;
}

INA226_Status_t INA226_DeviceReadShuntVoltage_mV(INA226_Handle_t *dev, float *voltage_mv)
{
    uint16_t raw_u16 = 0U;
    int16_t raw;
    INA226_Status_t status;

    if ((dev == NULL) || (voltage_mv == NULL))
    {
        return INA226_ERR_PARAM;
    }

    if (dev->is_ready == 0U)
    {
        return INA226_ERR_NOT_READY;
    }

    status = INA226_ReadReg16(dev, INA226_REG_SHUNT_VOLTAGE, &raw_u16);
    if (status != INA226_OK)
    {
        return status;
    }

    raw = (int16_t)raw_u16;
    *voltage_mv = (float)raw * 0.0025f;
    return INA226_OK;
}

INA226_Status_t INA226_DeviceReadCurrent_A(INA226_Handle_t *dev, float *current_a)
{
    uint16_t raw_u16 = 0U;
    int16_t raw;
    INA226_Status_t status;

    if ((dev == NULL) || (current_a == NULL))
    {
        return INA226_ERR_PARAM;
    }

    if (dev->is_ready == 0U)
    {
        return INA226_ERR_NOT_READY;
    }

    status = INA226_ReadReg16(dev, INA226_REG_CURRENT, &raw_u16);
    if (status != INA226_OK)
    {
        return status;
    }

    raw = (int16_t)raw_u16;
    *current_a = (float)raw * dev->current_lsb_a;
    return INA226_OK;
}

INA226_Status_t INA226_DeviceReadPower_W(INA226_Handle_t *dev, float *power_w)
{
    uint16_t raw = 0U;
    INA226_Status_t status;

    if ((dev == NULL) || (power_w == NULL))
    {
        return INA226_ERR_PARAM;
    }

    if (dev->is_ready == 0U)
    {
        return INA226_ERR_NOT_READY;
    }

    status = INA226_ReadReg16(dev, INA226_REG_POWER, &raw);
    if (status != INA226_OK)
    {
        return status;
    }

    *power_w = (float)raw * dev->power_lsb_w;
    return INA226_OK;
}

INA226_Status_t INA226_DeviceReadAll(INA226_Handle_t *dev, INA226_Data_t *data)
{
    INA226_Status_t status;

    if ((dev == NULL) || (data == NULL))
    {
        return INA226_ERR_PARAM;
    }

    status = INA226_DeviceReadBusVoltage_V(dev, &data->bus_voltage_v);
    if (status != INA226_OK)
    {
        return status;
    }

    status = INA226_DeviceReadShuntVoltage_mV(dev, &data->shunt_voltage_mv);
    if (status != INA226_OK)
    {
        return status;
    }

    status = INA226_DeviceReadCurrent_A(dev, &data->current_a);
    if (status != INA226_OK)
    {
        return status;
    }

    status = INA226_DeviceReadPower_W(dev, &data->power_w);
    if (status != INA226_OK)
    {
        return status;
    }

    return INA226_OK;
}

HAL_StatusTypeDef INA226_Init(void)
{
    uint8_t detected_address_7bit = 0U;
    uint16_t calibration = 0U;
    uint16_t verify_reg = 0U;
    INA226_Status_t status;

    g_ina_device.hi2c = &g_ina_i2c;
    g_ina_device.i2c_addr = 0U;
    g_ina_device.shunt_ohm = 0.0f;
    g_ina_device.current_lsb_a = 0.0f;
    g_ina_device.power_lsb_w = 0.0f;
    g_ina_device.is_ready = 0U;

    g_ina_diag.ready = 0U;
    g_ina_diag.detected_address_7bit = 0U;
    g_ina_diag.last_stage = (uint8_t)INA226_DIAG_STAGE_NONE;
    g_ina_diag.last_hal_status = (uint8_t)HAL_OK;
    g_ina_diag.last_hal_error_code = HAL_I2C_ERROR_NONE;
    g_ina_calibration = 0U;

    if (INA226_I2C2_Init() != HAL_OK)
    {
        return HAL_ERROR;
    }

    if (INA226_ScanAddress(&detected_address_7bit) != HAL_OK)
    {
        return HAL_ERROR;
    }

    status = INA226_PrepareDevice(&g_ina_device,
                                  &g_ina_i2c,
                                  (uint16_t)(detected_address_7bit << 1),
                                  INA226_SHUNT_RESISTOR_OHM,
                                  INA226_CURRENT_LSB_A,
                                  &calibration);
    if (status != INA226_OK)
    {
        INA226_RecordDriverFailure(INA226_DIAG_STAGE_I2C_INIT, status);
        return HAL_ERROR;
    }

    g_ina_calibration = calibration;
    g_ina_diag.detected_address_7bit = detected_address_7bit;

    status = INA226_WriteReg16(&g_ina_device, INA226_REG_CONFIG, INA226_CONFIG_DEFAULT);
    if (status != INA226_OK)
    {
        INA226_RecordDriverFailure(INA226_DIAG_STAGE_WRITE_CONFIG, status);
        return HAL_ERROR;
    }

    status = INA226_WriteReg16(&g_ina_device, INA226_REG_CALIBRATION, g_ina_calibration);
    if (status != INA226_OK)
    {
        INA226_RecordDriverFailure(INA226_DIAG_STAGE_WRITE_CALIBRATION, status);
        return HAL_ERROR;
    }

    status = INA226_ReadReg16(&g_ina_device, INA226_REG_CONFIG, &verify_reg);
    if (status != INA226_OK)
    {
        INA226_RecordDriverFailure(INA226_DIAG_STAGE_READ_CONFIG, status);
        return HAL_ERROR;
    }
    if (verify_reg != INA226_CONFIG_DEFAULT)
    {
        INA226_RecordLogicFailure(INA226_DIAG_STAGE_READ_CONFIG);
        return HAL_ERROR;
    }

    status = INA226_ReadReg16(&g_ina_device, INA226_REG_CALIBRATION, &verify_reg);
    if (status != INA226_OK)
    {
        INA226_RecordDriverFailure(INA226_DIAG_STAGE_READ_CALIBRATION, status);
        return HAL_ERROR;
    }
    if (verify_reg != g_ina_calibration)
    {
        INA226_RecordLogicFailure(INA226_DIAG_STAGE_READ_CALIBRATION);
        return HAL_ERROR;
    }

    g_ina_device.is_ready = 1U;
    INA226_RecordSuccess();
    return HAL_OK;
}

HAL_StatusTypeDef INA226_ReadCurrent_mA(int32_t *current_mA_x100)
{
    float current_a = 0.0f;
    INA226_Status_t status;

    if (current_mA_x100 == NULL)
    {
        INA226_RecordDriverFailure(INA226_DIAG_STAGE_READ_CURRENT, INA226_ERR_PARAM);
        return HAL_ERROR;
    }

    if (INA226_EnsureCalibration() != HAL_OK)
    {
        return HAL_ERROR;
    }

    status = INA226_DeviceReadCurrent_A(&g_ina_device, &current_a);
    if (status != INA226_OK)
    {
        INA226_RecordDriverFailure(INA226_DIAG_STAGE_READ_CURRENT, status);
        return HAL_ERROR;
    }

    *current_mA_x100 = INA226_RoundFloatToInt32(current_a * 100000.0f);
    INA226_RecordSuccess();
    return HAL_OK;
}

HAL_StatusTypeDef INA226_ReadBusVoltage_mV(uint16_t *bus_voltage_mV)
{
    float voltage_v = 0.0f;
    INA226_Status_t status;
    int32_t scaled_voltage;

    if (bus_voltage_mV == NULL)
    {
        INA226_RecordDriverFailure(INA226_DIAG_STAGE_READ_BUS, INA226_ERR_PARAM);
        return HAL_ERROR;
    }

    status = INA226_DeviceReadBusVoltage_V(&g_ina_device, &voltage_v);
    if (status != INA226_OK)
    {
        INA226_RecordDriverFailure(INA226_DIAG_STAGE_READ_BUS, status);
        return HAL_ERROR;
    }

    scaled_voltage = INA226_RoundFloatToInt32(voltage_v * 1000.0f);
    if (scaled_voltage < 0)
    {
        scaled_voltage = 0;
    }

    *bus_voltage_mV = (uint16_t)scaled_voltage;
    INA226_RecordSuccess();
    return HAL_OK;
}

HAL_StatusTypeDef INA226_ReadShunt_uV(int32_t *shunt_uV)
{
    float shunt_mv = 0.0f;
    INA226_Status_t status;

    if (shunt_uV == NULL)
    {
        INA226_RecordDriverFailure(INA226_DIAG_STAGE_READ_SHUNT, INA226_ERR_PARAM);
        return HAL_ERROR;
    }

    status = INA226_DeviceReadShuntVoltage_mV(&g_ina_device, &shunt_mv);
    if (status != INA226_OK)
    {
        INA226_RecordDriverFailure(INA226_DIAG_STAGE_READ_SHUNT, status);
        return HAL_ERROR;
    }

    *shunt_uV = INA226_RoundFloatToInt32(shunt_mv * 1000.0f);
    INA226_RecordSuccess();
    return HAL_OK;
}

GPIO_PinState INA226_ReadAlertLevel(void)
{
    return HAL_GPIO_ReadPin(INA226_ALERT_GPIO_PORT, INA226_ALERT_GPIO_PIN);
}

void INA226_GetDiag(INA226_Diag_t *diag)
{
    if (diag != NULL)
    {
        *diag = g_ina_diag;
    }
}
