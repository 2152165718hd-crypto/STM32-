#include "APPLICATION/PVClean/PVClean.h"

#include "Hardware/INA226/INA226.h"
#include "Hardware/LightSensor/LightSensor.h"
#include "Hardware/Motor/Motor.h"

#include <stdio.h>
#include <string.h>

#define PVCLEAN_SAMPLE_INTERVAL_MS 200u
#define PVCLEAN_INA_RETRY_INTERVAL_MS 1000u
#define PVCLEAN_REPORT_INTERVAL_MS 1000u
#define PVCLEAN_STATUS_TEXT_SIZE 64u

typedef enum
{
    PVCLEAN_TRIGGER_NONE = 0,
    PVCLEAN_TRIGGER_MANUAL,
    PVCLEAN_TRIGGER_AUTO,
    PVCLEAN_TRIGGER_TIMER
} PVCleanTrigger_t;

static PVCleanConfig_t g_config = {
    70,
    8,
    12,
    15,
    40,
    600,
    70,
    10
};

static PVCleanStatus_t g_status;
static uint16_t g_filtered_light_raw = 0u;
static int32_t g_filtered_current_x100 = 0;
static uint8_t g_filter_seeded = 0u;
static uint8_t g_cleaning_active = 0u;
static uint8_t g_auto_rearm_required = 0u;
static uint8_t g_light_ready = 0u;
static uint8_t g_ina_ready = 0u;
static uint32_t g_last_sample_tick = 0u;
static uint32_t g_last_ina_retry_tick = 0u;
static uint32_t g_last_timer_anchor_tick = 0u;
static uint32_t g_clean_deadline_tick = 0u;
static uint32_t g_dirty_condition_start_tick = 0u;
static uint32_t g_last_report_tick = 0u;

static void PVClean_ClampConfig(void);
static void PVClean_SampleSensors(void);
static void PVClean_ProcessTimer(uint32_t now);
static void PVClean_ProcessAutoJudge(uint32_t now);
static void PVClean_StartCleaning(PVCleanTrigger_t trigger);
static void PVClean_StopCleaning(void);
static uint8_t PVClean_IsNight(void);
static void PVClean_UpdateStateFlags(void);
static void PVClean_UpdateInaDiag(uint8_t sample_valid, const INA226_Diag_t *sample_failure_diag);
static void PVClean_ReportStatusIfDue(uint32_t now);
static void PVClean_BroadcastStatusText(void);
static void PVClean_SendStatusText(uint8_t link_id);
static void PVClean_BuildStatusText(char *buffer, uint32_t size);
static void PVClean_FormatCurrent(char *buffer, uint32_t size, int32_t value_x100);
static uint16_t PVClean_ComputeExpectedCurrent_x100(uint8_t light_percent);

HAL_StatusTypeDef PVClean_Init(void)
{
    HAL_StatusTypeDef init_status = HAL_OK;

    memset(&g_status, 0, sizeof(g_status));
    g_status.mode = PVCLEAN_MODE_AUTO;
    g_status.clean_state = PVCLEAN_STATE_IDLE;

    PVClean_ClampConfig();
    Motor_Init();

    if (Light_Init() != HAL_OK)
    {
        init_status = HAL_ERROR;
        g_light_ready = 0u;
    }
    else
    {
        g_light_ready = 1u;
    }

    if (INA226_Init() != HAL_OK)
    {
        init_status = HAL_ERROR;
        g_ina_ready = 0u;
        g_last_ina_retry_tick = HAL_GetTick();
    }
    else
    {
        g_ina_ready = 1u;
        g_last_ina_retry_tick = HAL_GetTick();
    }

    ESP_RegisterCallback(NULL);
    (void)ESP_Init();

    g_last_timer_anchor_tick = HAL_GetTick();
    g_last_report_tick = g_last_timer_anchor_tick;
    PVClean_SampleSensors();
    PVClean_UpdateStateFlags();
    return init_status;
}

void PVClean_Process(void)
{
    uint32_t now = HAL_GetTick();

    ESP_Process();

    if ((now - g_last_sample_tick) >= PVCLEAN_SAMPLE_INTERVAL_MS)
    {
        g_last_sample_tick = now;
        PVClean_SampleSensors();
    }

    if (g_cleaning_active != 0u)
    {
        if ((int32_t)(now - g_clean_deadline_tick) >= 0)
        {
            PVClean_StopCleaning();
        }
        else
        {
            uint32_t remaining_ms = g_clean_deadline_tick - now;
            g_status.clean_remaining_sec = (remaining_ms + 999u) / 1000u;
        }
    }
    else
    {
        g_status.clean_remaining_sec = 0u;
    }

    PVClean_ProcessTimer(now);
    PVClean_ProcessAutoJudge(now);

    g_status.wifi_state = ESP_GetConnectionState();
    g_status.wifi_clients = ESP_GetClientCount();
    PVClean_UpdateStateFlags();
    PVClean_ReportStatusIfDue(now);
}

PVCleanConfig_t *PVClean_GetConfig(void)
{
    return &g_config;
}

void PVClean_GetStatus(PVCleanStatus_t *status)
{
    if (status != NULL)
    {
        *status = g_status;
    }
}

void PVClean_SetMode(PVCleanMode_t mode)
{
    if (mode > PVCLEAN_MODE_TIMER)
    {
        return;
    }

    if (g_cleaning_active != 0u)
    {
        PVClean_StopCleaning();
    }

    g_status.mode = mode;
    g_dirty_condition_start_tick = 0u;
    g_auto_rearm_required = 0u;

    if (mode == PVCLEAN_MODE_TIMER)
    {
        PVClean_ResetTimerSchedule();
    }
    else
    {
        g_status.timer_pending = 0u;
        g_status.timer_remaining_sec = 0u;
    }

    PVClean_UpdateStateFlags();
}

PVCleanMode_t PVClean_GetMode(void)
{
    return g_status.mode;
}

void PVClean_RequestManualStart(void)
{
    PVClean_StartCleaning(PVCLEAN_TRIGGER_MANUAL);
}

void PVClean_RequestStop(void)
{
    PVClean_StopCleaning();
}

void PVClean_ApplyConfig(void)
{
    PVClean_ClampConfig();

    if (g_cleaning_active != 0u)
    {
        Motor_SetAllSpeed((int8_t)g_config.motor_speed_percent);
    }

    g_status.expected_current_mA_x100 = PVClean_ComputeExpectedCurrent_x100(g_status.light_percent);
    PVClean_UpdateStateFlags();
}

void PVClean_ResetTimerSchedule(void)
{
    g_last_timer_anchor_tick = HAL_GetTick();
    g_status.timer_pending = 0u;
    g_status.timer_remaining_sec = (uint32_t)g_config.timer_interval_h * 3600u;
    PVClean_UpdateStateFlags();
}

const char *PVClean_ModeString(PVCleanMode_t mode)
{
    switch (mode)
    {
    case PVCLEAN_MODE_AUTO:
        return "AUTO";
    case PVCLEAN_MODE_MANUAL:
        return "MAN";
    case PVCLEAN_MODE_TIMER:
        return "TIMER";
    default:
        return "UNK";
    }
}

const char *PVClean_StateString(PVCleanState_t state)
{
    switch (state)
    {
    case PVCLEAN_STATE_IDLE:
        return "IDLE";
    case PVCLEAN_STATE_RUNNING:
        return "RUN";
    case PVCLEAN_STATE_PENDING_DAYLIGHT:
        return "PEND";
    case PVCLEAN_STATE_NIGHT_LOCK:
        return "LOCK";
    default:
        return "UNK";
    }
}

static void PVClean_ClampConfig(void)
{
    if (g_config.motor_speed_percent < 10)
    {
        g_config.motor_speed_percent = 10;
    }
    else if (g_config.motor_speed_percent > 100)
    {
        g_config.motor_speed_percent = 100;
    }

    if (g_config.clean_duration_sec < 1)
    {
        g_config.clean_duration_sec = 1;
    }
    else if (g_config.clean_duration_sec > 120)
    {
        g_config.clean_duration_sec = 120;
    }

    if (g_config.timer_interval_h < 1)
    {
        g_config.timer_interval_h = 1;
    }
    else if (g_config.timer_interval_h > 72)
    {
        g_config.timer_interval_h = 72;
    }

    if (g_config.night_threshold_percent < 0)
    {
        g_config.night_threshold_percent = 0;
    }
    else if (g_config.night_threshold_percent > 100)
    {
        g_config.night_threshold_percent = 100;
    }

    if (g_config.auto_light_threshold_percent < 1)
    {
        g_config.auto_light_threshold_percent = 1;
    }
    else if (g_config.auto_light_threshold_percent > 100)
    {
        g_config.auto_light_threshold_percent = 100;
    }

    if (g_config.max_expected_current_mA_x10 < 1)
    {
        g_config.max_expected_current_mA_x10 = 1;
    }
    else if (g_config.max_expected_current_mA_x10 > 600)
    {
        g_config.max_expected_current_mA_x10 = 600;
    }

    if (g_config.dirty_ratio_percent < 10)
    {
        g_config.dirty_ratio_percent = 10;
    }
    else if (g_config.dirty_ratio_percent > 100)
    {
        g_config.dirty_ratio_percent = 100;
    }

    if (g_config.confirm_seconds < 1)
    {
        g_config.confirm_seconds = 1;
    }
    else if (g_config.confirm_seconds > 60)
    {
        g_config.confirm_seconds = 60;
    }
}

static uint16_t PVClean_ComputeExpectedCurrent_x100(uint8_t light_percent)
{
    return (uint16_t)(((g_config.max_expected_current_mA_x10 * (int32_t)light_percent) + 5) / 10);
}

static void PVClean_SampleSensors(void)
{
    uint32_t now = HAL_GetTick();
    uint16_t light_raw = 0u;
    int32_t current_x100 = 0;
    uint16_t bus_voltage_mV = 0u;
    int32_t shunt_uV = 0;
    uint8_t ina_current_valid = 0u;
    uint8_t ina_bus_valid = 0u;
    uint8_t ina_shunt_valid = 0u;
    uint8_t ina_failure_captured = 0u;
    INA226_Diag_t ina_failure_diag = {0};

    if ((g_ina_ready == 0u) && ((now - g_last_ina_retry_tick) >= PVCLEAN_INA_RETRY_INTERVAL_MS))
    {
        g_last_ina_retry_tick = now;
        g_ina_ready = (INA226_Init() == HAL_OK) ? 1u : 0u;
    }

    if (g_light_ready != 0u)
    {
        light_raw = Light_ReadRaw();
        g_status.light_digital = (uint8_t)(Light_ReadDigital() == GPIO_PIN_SET);
    }
    else
    {
        g_status.light_digital = 0u;
    }

    if (g_ina_ready != 0u)
    {
        if (INA226_ReadCurrent_mA(&current_x100) == HAL_OK)
        {
            ina_current_valid = 1u;
        }
        else
        {
            current_x100 = g_status.current_mA_x100;
            if (ina_failure_captured == 0u)
            {
                INA226_GetDiag(&ina_failure_diag);
                ina_failure_captured = 1u;
            }
        }

        if (INA226_ReadBusVoltage_mV(&bus_voltage_mV) == HAL_OK)
        {
            ina_bus_valid = 1u;
        }
        else
        {
            bus_voltage_mV = g_status.bus_voltage_mV;
            if (ina_failure_captured == 0u)
            {
                INA226_GetDiag(&ina_failure_diag);
                ina_failure_captured = 1u;
            }
        }

        if (INA226_ReadShunt_uV(&shunt_uV) == HAL_OK)
        {
            ina_shunt_valid = 1u;
        }
        else
        {
            shunt_uV = g_status.shunt_uV;
            if (ina_failure_captured == 0u)
            {
                INA226_GetDiag(&ina_failure_diag);
                ina_failure_captured = 1u;
            }
        }

        g_status.alert_level = (uint8_t)(INA226_ReadAlertLevel() == GPIO_PIN_SET);

        if ((ina_current_valid == 0u) && (ina_bus_valid == 0u) && (ina_shunt_valid == 0u))
        {
            g_last_ina_retry_tick = now;
            g_ina_ready = (INA226_Init() == HAL_OK) ? 1u : 0u;
        }
    }
    else
    {
        current_x100 = g_status.current_mA_x100;
        bus_voltage_mV = g_status.bus_voltage_mV;
        shunt_uV = g_status.shunt_uV;
        g_status.alert_level = 0u;
    }

    if (current_x100 < 0)
    {
        current_x100 = 0;
    }

    if (g_filter_seeded == 0u)
    {
        g_filtered_light_raw = light_raw;
        g_filtered_current_x100 = current_x100;
        g_filter_seeded = 1u;
    }
    else
    {
        g_filtered_light_raw = (uint16_t)((g_filtered_light_raw * 3u + light_raw + 2u) / 4u);
        g_filtered_current_x100 = (g_filtered_current_x100 * 3 + current_x100) / 4;
    }

    g_status.light_raw = g_filtered_light_raw;
    g_status.light_percent = Light_ConvertRawToPercent(g_filtered_light_raw);
    g_status.current_mA_x100 = g_filtered_current_x100;
    g_status.expected_current_mA_x100 = PVClean_ComputeExpectedCurrent_x100(g_status.light_percent);
    g_status.bus_voltage_mV = bus_voltage_mV;
    g_status.shunt_uV = shunt_uV;
    PVClean_UpdateInaDiag((uint8_t)((ina_current_valid != 0u) && (ina_bus_valid != 0u) && (ina_shunt_valid != 0u)),
                          (ina_failure_captured != 0u) ? &ina_failure_diag : NULL);
}

static void PVClean_UpdateInaDiag(uint8_t sample_valid, const INA226_Diag_t *sample_failure_diag)
{
    INA226_Diag_t diag;

    INA226_GetDiag(&diag);
    if ((sample_valid == 0u) &&
        (sample_failure_diag != NULL) &&
        (diag.ready != 0u) &&
        ((INA226_DiagStage_t)diag.last_stage == INA226_DIAG_STAGE_NONE))
    {
        diag = *sample_failure_diag;
    }

    g_status.ina_ready = diag.ready;
    g_status.ina_data_valid = sample_valid;
    g_status.ina_addr_7bit = diag.detected_address_7bit;
    g_status.ina_last_stage = diag.last_stage;
    g_status.ina_last_hal_status = diag.last_hal_status;
    g_status.ina_last_error_code = diag.last_hal_error_code;
}

static void PVClean_ProcessTimer(uint32_t now)
{
    uint32_t interval_ms;
    uint32_t elapsed;

    if (g_status.mode != PVCLEAN_MODE_TIMER)
    {
        g_status.timer_pending = 0u;
        g_status.timer_remaining_sec = 0u;
        return;
    }

    interval_ms = (uint32_t)g_config.timer_interval_h * 3600000u;
    elapsed = now - g_last_timer_anchor_tick;

    if (g_status.timer_pending != 0u)
    {
        g_status.timer_remaining_sec = 0u;

        if ((g_cleaning_active == 0u) && (PVClean_IsNight() == 0u))
        {
            PVClean_StartCleaning(PVCLEAN_TRIGGER_TIMER);
        }

        return;
    }

    if (elapsed >= interval_ms)
    {
        if (PVClean_IsNight() != 0u)
        {
            g_status.timer_pending = 1u;
            g_status.timer_remaining_sec = 0u;
        }
        else if (g_cleaning_active == 0u)
        {
            PVClean_StartCleaning(PVCLEAN_TRIGGER_TIMER);
        }
    }
    else
    {
        g_status.timer_remaining_sec = (interval_ms - elapsed + 999u) / 1000u;
    }
}

static void PVClean_ProcessAutoJudge(uint32_t now)
{
    uint8_t condition_met = 0u;
    int32_t threshold_x100;

    if ((g_light_ready == 0u) || (g_ina_ready == 0u) || (g_status.ina_data_valid == 0u) ||
        (g_status.mode != PVCLEAN_MODE_AUTO) || (g_cleaning_active != 0u) || (PVClean_IsNight() != 0u))
    {
        g_dirty_condition_start_tick = 0u;

        if (g_status.mode != PVCLEAN_MODE_AUTO)
        {
            g_auto_rearm_required = 0u;
        }

        return;
    }

    threshold_x100 = ((int32_t)g_status.expected_current_mA_x100 * g_config.dirty_ratio_percent) / 100;

    if ((g_status.light_percent >= (uint8_t)g_config.auto_light_threshold_percent) &&
        (g_status.current_mA_x100 < threshold_x100))
    {
        condition_met = 1u;
    }

    if (condition_met == 0u)
    {
        g_dirty_condition_start_tick = 0u;
        g_auto_rearm_required = 0u;
        return;
    }

    if (g_auto_rearm_required != 0u)
    {
        return;
    }

    if (g_dirty_condition_start_tick == 0u)
    {
        g_dirty_condition_start_tick = now;
        return;
    }

    if ((now - g_dirty_condition_start_tick) >= ((uint32_t)g_config.confirm_seconds * 1000u))
    {
        PVClean_StartCleaning(PVCLEAN_TRIGGER_AUTO);
        g_dirty_condition_start_tick = 0u;
        g_auto_rearm_required = 1u;
    }
}

static void PVClean_StartCleaning(PVCleanTrigger_t trigger)
{
    uint32_t now;

    if (g_cleaning_active != 0u)
    {
        return;
    }

    if ((trigger != PVCLEAN_TRIGGER_MANUAL) && (PVClean_IsNight() != 0u))
    {
        if (trigger == PVCLEAN_TRIGGER_TIMER)
        {
            g_status.timer_pending = 1u;
        }
        return;
    }

    now = HAL_GetTick();
    Motor_SetAllSpeed((int8_t)g_config.motor_speed_percent);
    g_cleaning_active = 1u;
    g_clean_deadline_tick = now + (uint32_t)g_config.clean_duration_sec * 1000u;
    g_status.clean_remaining_sec = (uint32_t)g_config.clean_duration_sec;

    if (trigger == PVCLEAN_TRIGGER_TIMER)
    {
        g_last_timer_anchor_tick = now;
        g_status.timer_pending = 0u;
        g_status.timer_remaining_sec = (uint32_t)g_config.timer_interval_h * 3600u;
    }

    if (trigger == PVCLEAN_TRIGGER_AUTO)
    {
        g_auto_rearm_required = 1u;
    }

    PVClean_UpdateStateFlags();
}

static void PVClean_StopCleaning(void)
{
    if (g_cleaning_active == 0u)
    {
        return;
    }

    Motor_Stop();
    g_cleaning_active = 0u;
    g_status.clean_remaining_sec = 0u;
    PVClean_UpdateStateFlags();
}

static uint8_t PVClean_IsNight(void)
{
    if (g_light_ready == 0u)
    {
        return 0u;
    }

    return (uint8_t)(g_status.light_percent < (uint8_t)g_config.night_threshold_percent);
}

static void PVClean_UpdateStateFlags(void)
{
    g_status.night_locked = (uint8_t)((g_status.mode != PVCLEAN_MODE_MANUAL) && (PVClean_IsNight() != 0u));

    if (g_cleaning_active != 0u)
    {
        g_status.clean_state = PVCLEAN_STATE_RUNNING;
    }
    else if (g_status.timer_pending != 0u)
    {
        g_status.clean_state = PVCLEAN_STATE_PENDING_DAYLIGHT;
    }
    else if (g_status.night_locked != 0u)
    {
        g_status.clean_state = PVCLEAN_STATE_NIGHT_LOCK;
    }
    else
    {
        g_status.clean_state = PVCLEAN_STATE_IDLE;
    }
}

static void PVClean_ReportStatusIfDue(uint32_t now)
{
    if (g_status.wifi_clients == 0u)
    {
        return;
    }

    if ((now - g_last_report_tick) < PVCLEAN_REPORT_INTERVAL_MS)
    {
        return;
    }

    g_last_report_tick = now;
    PVClean_BroadcastStatusText();
}

static void PVClean_BroadcastStatusText(void)
{
    uint8_t link_mask;
    uint8_t link_id;

    link_mask = ESP_GetActiveLinkMask();
    for (link_id = 0u; link_id < ESP_MAX_LINKS; link_id++)
    {
        if ((link_mask & (uint8_t)(1u << link_id)) != 0u)
        {
            PVClean_SendStatusText(link_id);
        }
    }
}

static void PVClean_SendStatusText(uint8_t link_id)
{
    char text[PVCLEAN_STATUS_TEXT_SIZE];

    if (ESP_IsLinkActive(link_id) == 0u)
    {
        return;
    }

    PVClean_BuildStatusText(text, sizeof(text));
    (void)ESP_SendString(link_id, text);
}

static void PVClean_BuildStatusText(char *buffer, uint32_t size)
{
    char current_text[16];

    if ((buffer == NULL) || (size == 0u))
    {
        return;
    }

    PVClean_FormatCurrent(current_text, sizeof(current_text), g_status.current_mA_x100);
    snprintf(buffer,
             size,
             "LIGHT=%u,CURRENT_MA=%s,CLEAN=%s\r\n",
             g_status.light_percent,
             current_text,
             PVClean_StateString(g_status.clean_state));
}

static void PVClean_FormatCurrent(char *buffer, uint32_t size, int32_t value_x100)
{
    int32_t abs_value;

    if ((buffer == NULL) || (size == 0u))
    {
        return;
    }

    abs_value = (value_x100 >= 0) ? value_x100 : -value_x100;
    snprintf(buffer,
             size,
             "%s%ld.%02ld",
             (value_x100 < 0) ? "-" : "",
             (long)(abs_value / 100),
             (long)(abs_value % 100));
}
