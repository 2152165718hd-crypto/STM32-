#ifndef __INA226_H
#define __INA226_H

#include "stm32f1xx_hal.h"
#include <stdint.h>

#define INA226_ADDR_7BIT              0x40U
#define INA226_I2C_ADDR               (INA226_ADDR_7BIT << 1)

#define INA226_SHUNT_RESISTOR_OHM     0.1f
#define INA226_CURRENT_LSB_A          0.00001f

#define INA226_REG_CONFIG             0x00U
#define INA226_REG_SHUNT_VOLTAGE      0x01U
#define INA226_REG_BUS_VOLTAGE        0x02U
#define INA226_REG_POWER              0x03U
#define INA226_REG_CURRENT            0x04U
#define INA226_REG_CALIBRATION        0x05U
#define INA226_REG_MASK_ENABLE        0x06U
#define INA226_REG_ALERT_LIMIT        0x07U
#define INA226_REG_MANUFACTURER_ID    0xFEU
#define INA226_REG_DIE_ID             0xFFU

#define INA226_CONFIG_DEFAULT         0x4927U
#define INA226_I2C_TIMEOUT_MS         100U

typedef enum
{
    INA226_OK = 0,
    INA226_ERR_PARAM,
    INA226_ERR_I2C,
    INA226_ERR_NOT_READY
} INA226_Status_t;

typedef struct
{
    I2C_HandleTypeDef *hi2c;
    uint16_t i2c_addr;
    float shunt_ohm;
    float current_lsb_a;
    float power_lsb_w;
    uint8_t is_ready;
} INA226_Handle_t;

typedef struct
{
    float bus_voltage_v;
    float shunt_voltage_mv;
    float current_a;
    float power_w;
} INA226_Data_t;

typedef enum
{
    INA226_DIAG_STAGE_NONE = 0,
    INA226_DIAG_STAGE_I2C_INIT,
    INA226_DIAG_STAGE_SCAN_ADDRESS,
    INA226_DIAG_STAGE_WRITE_CONFIG,
    INA226_DIAG_STAGE_WRITE_CALIBRATION,
    INA226_DIAG_STAGE_READ_CONFIG,
    INA226_DIAG_STAGE_READ_CALIBRATION,
    INA226_DIAG_STAGE_READ_CURRENT,
    INA226_DIAG_STAGE_READ_BUS,
    INA226_DIAG_STAGE_READ_SHUNT,
    INA226_DIAG_STAGE_ENSURE_CALIB_READ,
    INA226_DIAG_STAGE_ENSURE_CALIB_WRITE
} INA226_DiagStage_t;

typedef struct
{
    uint8_t ready;
    uint8_t detected_address_7bit;
    uint8_t last_stage;
    uint8_t last_hal_status;
    uint32_t last_hal_error_code;
} INA226_Diag_t;

INA226_Status_t INA226_DeviceInit(INA226_Handle_t *dev,
                                  I2C_HandleTypeDef *hi2c,
                                  uint16_t i2c_addr,
                                  float shunt_ohm,
                                  float current_lsb_a);
INA226_Status_t INA226_DeviceReadBusVoltage_V(INA226_Handle_t *dev, float *voltage_v);
INA226_Status_t INA226_DeviceReadShuntVoltage_mV(INA226_Handle_t *dev, float *voltage_mv);
INA226_Status_t INA226_DeviceReadCurrent_A(INA226_Handle_t *dev, float *current_a);
INA226_Status_t INA226_DeviceReadPower_W(INA226_Handle_t *dev, float *power_w);
INA226_Status_t INA226_DeviceReadAll(INA226_Handle_t *dev, INA226_Data_t *data);

HAL_StatusTypeDef INA226_Init(void);
HAL_StatusTypeDef INA226_ReadCurrent_mA(int32_t *current_mA_x100);
HAL_StatusTypeDef INA226_ReadBusVoltage_mV(uint16_t *bus_voltage_mV);
HAL_StatusTypeDef INA226_ReadShunt_uV(int32_t *shunt_uV);
GPIO_PinState INA226_ReadAlertLevel(void);
void INA226_GetDiag(INA226_Diag_t *diag);

#endif
