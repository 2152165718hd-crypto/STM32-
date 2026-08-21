#ifndef __PVCLEAN_H__
#define __PVCLEAN_H__

#include "Hardware/ESP_01S/ESP_01S.h"
#include "stm32f1xx_hal.h"

#include <stdint.h>

typedef enum
{
    PVCLEAN_MODE_AUTO = 0,
    PVCLEAN_MODE_MANUAL,
    PVCLEAN_MODE_TIMER
} PVCleanMode_t;

typedef enum
{
    PVCLEAN_STATE_IDLE = 0,
    PVCLEAN_STATE_RUNNING,
    PVCLEAN_STATE_PENDING_DAYLIGHT,
    PVCLEAN_STATE_NIGHT_LOCK
} PVCleanState_t;

typedef struct
{
    int32_t motor_speed_percent;
    int32_t clean_duration_sec;
    int32_t timer_interval_h;
    int32_t night_threshold_percent;
    int32_t auto_light_threshold_percent;
    int32_t max_expected_current_mA_x10;
    int32_t dirty_ratio_percent;
    int32_t confirm_seconds;
} PVCleanConfig_t;

typedef struct
{
    PVCleanMode_t mode;
    PVCleanState_t clean_state;
    uint16_t light_raw;
    uint8_t light_percent;
    uint8_t light_digital;
    int32_t current_mA_x100;
    uint16_t expected_current_mA_x100;
    uint16_t bus_voltage_mV;
    int32_t shunt_uV;
    uint8_t alert_level;
    uint8_t ina_ready;
    uint8_t ina_data_valid;
    uint8_t ina_addr_7bit;
    uint8_t ina_last_stage;
    uint8_t ina_last_hal_status;
    uint32_t ina_last_error_code;
    uint8_t night_locked;
    uint8_t timer_pending;
    uint32_t timer_remaining_sec;
    uint32_t clean_remaining_sec;
    uint8_t wifi_clients;
    ESP_ConnectionState_t wifi_state;
} PVCleanStatus_t;

/*
 * Wi-Fi text reporting protocol:
 * Each connected TCP client receives one CRLF-terminated line every second.
 * Format:
 * LIGHT=<0-100>,CURRENT_MA=<mA.xx>,CLEAN=<IDLE|RUN|PEND|LOCK>\r\n
 *
 * Example:
 * LIGHT=78,CURRENT_MA=12.35,CLEAN=RUN\r\n
 */
HAL_StatusTypeDef PVClean_Init(void);
void PVClean_Process(void);
PVCleanConfig_t *PVClean_GetConfig(void);
void PVClean_GetStatus(PVCleanStatus_t *status);
void PVClean_SetMode(PVCleanMode_t mode);
PVCleanMode_t PVClean_GetMode(void);
void PVClean_RequestManualStart(void);
void PVClean_RequestStop(void);
void PVClean_ApplyConfig(void);
void PVClean_ResetTimerSchedule(void);
const char *PVClean_ModeString(PVCleanMode_t mode);
const char *PVClean_StateString(PVCleanState_t state);

#endif
