#include ".\APPLICATION\Menu\Menu.h"

#include ".\Hardware\AV_Alarm\AV_Alarm.h"
#include ".\Hardware\DHT11\DHT11.h"
#include ".\Hardware\ESP_01S\ESP_01S.h"
#include ".\Hardware\MQ2_Smoke\MQ2_Smoke.h"
#include ".\Hardware\SR602\SR602.h"
#include ".\OneNet_MQTT\onenet.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SENSOR_SAMPLE_PERIOD_MS 1000U
#define REPORT_PERIOD_DEFAULT_SEC 2
#define REMOTE_REPORT_DELAY_MS 800U

#define TEMP_MIN_VALUE 0
#define TEMP_MAX_VALUE 60
#define CLOUD_TEMP_MAX_VALUE 100
#define HUMI_MIN_VALUE 0
#define HUMI_MAX_VALUE 100
#define PCT_MIN_VALUE 0
#define PCT_MAX_VALUE 100

#define TEMP_HYSTERESIS 1
#define HUMI_HYSTERESIS 1
#define PCT_HYSTERESIS 3

#define HISTORY_SIZE 5U
#define HISTORY_TEXT_LEN 48U
#define MAX_ESP_LINKS 5U

#define ALARM_REASON_TEMP_LOW (1U << 0)
#define ALARM_REASON_TEMP_HIGH (1U << 1)
#define ALARM_REASON_HUMI_LOW (1U << 2)
#define ALARM_REASON_HUMI_HIGH (1U << 3)
#define ALARM_REASON_SMOKE_HIGH (1U << 4)
#define ALARM_REASON_PIR_INTRUSION (1U << 5)

typedef enum
{
    ALARM_MODE_AUTO = 0,
    ALARM_MODE_FORCE_ON,
    ALARM_MODE_FORCE_OFF
} AlarmMode_t;

typedef enum
{
    CONTROL_SRC_AUTO = 0,
    CONTROL_SRC_LOCAL,
    CONTROL_SRC_REMOTE
} ControlSource_t;

typedef enum
{
    SECURITY_MODE_NORMAL = 0,
    SECURITY_MODE_ARMED
} SecurityMode_t;

typedef struct
{
    int32_t temperature;
    int32_t humidity;
    uint8_t smoke_pct;
    uint16_t smoke_raw;
    uint8_t pir_active;
    uint8_t pir_ready;
    uint8_t pir_raw;
    uint8_t dht_valid;
    uint32_t update_tick;
} SensorSnapshot_t;

typedef struct
{
    int32_t temp_low;
    int32_t temp_high;
    int32_t humi_low;
    int32_t humi_high;
    int32_t smoke_high_pct;
} ThresholdConfig_t;

typedef struct
{
    AlarmMode_t mode;
    ControlSource_t control_source;
    uint32_t last_control_tick;
    uint8_t auto_alarm;
    uint8_t output_on;
    uint8_t reason_flags;
    uint8_t temp_low_active;
    uint8_t temp_high_active;
    uint8_t humi_low_active;
    uint8_t humi_high_active;
    uint8_t smoke_high_active;
    uint8_t pir_intrusion_active;
} AlarmControlState_t;

typedef struct
{
    char items[HISTORY_SIZE][HISTORY_TEXT_LEN];
    uint8_t head;
    uint8_t count;
} CommandHistory_t;

static SensorSnapshot_t g_sensor = {0};
static ThresholdConfig_t g_threshold = {18, 30, 30, 80, 60};
static AlarmControlState_t g_alarm = {ALARM_MODE_AUTO, CONTROL_SRC_AUTO, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
static CommandHistory_t g_history = {{{0}}, 0U, 0U};

static uint8_t g_manual_mode = 0U;
static uint8_t g_manual_alarm_on = 0U;
static SecurityMode_t g_security_mode = SECURITY_MODE_NORMAL;
static int32_t g_report_period_sec = REPORT_PERIOD_DEFAULT_SEC;

static uint8_t g_known_link[MAX_ESP_LINKS] = {0U};

static uint32_t g_last_sample_tick = 0U;
static uint32_t g_last_report_tick = 0U;
static uint8_t g_report_pending = 0U;
static uint32_t g_report_due_tick = 0U;

static MF_Menu_t *g_root_menu = NULL;
static MF_Menu_t *g_menu_sensor = NULL;
static MF_Menu_t *g_menu_threshold = NULL;
static MF_Menu_t *g_menu_history = NULL;
static MF_Menu_t *g_menu_cursor = NULL;
static MF_Menu_t *g_menu_alarm = NULL;
static MF_Menu_t *g_menu_security = NULL;

static int32_t Menu_ClampI32(int32_t value, int32_t min_value, int32_t max_value);
static uint8_t Menu_RawToPercent(uint16_t adc_raw);

static void Menu_NormalizeThresholds(void);
static void Menu_UpdatePirSensor(void);
static void Menu_SampleSensors(void);
static void Menu_UpdateAutoAlarm(void);
static void Menu_ApplyAlarmOutput(void);
static void Menu_SetAlarmMode(AlarmMode_t mode, ControlSource_t source);
static void Menu_SetSecurityMode(SecurityMode_t mode);

static void Menu_BuildMenus(void);

static void Menu_StrTrim(char *text);
static void Menu_StrToUpper(char *text);
static uint8_t Menu_ParseI32(const char *text, int32_t *out_value);

static void Menu_HistoryAdd(const char *line);
static const char *Menu_HistoryGetNewest(uint8_t index);

static const char *Menu_AlarmModeText(AlarmMode_t mode);
static const char *Menu_ControlSourceText(ControlSource_t source);
static const char *Menu_SecurityModeText(SecurityMode_t mode);
static const char *Menu_AlarmReasonText(uint8_t reason_flags);

static void Menu_FormatDataMessage(char *buffer, uint16_t size, uint8_t with_ok_prefix);
static void Menu_RequestTelemetryReport(uint32_t delay_ms);
static uint8_t Menu_SendPeriodicData(void);
static void Menu_SendOk(uint8_t link_id, const char *message);
static void Menu_SendErr(uint8_t link_id, const char *code, const char *message);

static void Menu_ProcessCommandLine(uint8_t link_id, char *line);

static void Menu_ActionCursorReverse(void);
static void Menu_ActionCursorBox(void);
static void Menu_ActionSecurityNormal(void);
static void Menu_ActionSecurityArmed(void);
static void Menu_OnThresholdChanged(int32_t value);
static void Menu_OnManualModeChanged(void);
static void Menu_OnManualAlarmChanged(void);

static void Menu_PageTempHumi(KeyEvent_t key, uint8_t *exit_flag);
static void Menu_PagePIR(KeyEvent_t key, uint8_t *exit_flag);
static void Menu_PageSmoke(KeyEvent_t key, uint8_t *exit_flag);
static void Menu_PageHistory(KeyEvent_t key, uint8_t *exit_flag);
static void Menu_PageWiFiDebug(KeyEvent_t key, uint8_t *exit_flag);
static void Menu_PageAlarmStatus(KeyEvent_t key, uint8_t *exit_flag);

static int32_t Menu_ClampI32(int32_t value, int32_t min_value, int32_t max_value)
{
    if (value < min_value)
    {
        return min_value;
    }
    if (value > max_value)
    {
        return max_value;
    }
    return value;
}

static uint8_t Menu_RawToPercent(uint16_t adc_raw)
{
    uint32_t pct = ((uint32_t)adc_raw * 100U + 2047U) / 4095U;
    if (pct > 100U)
    {
        pct = 100U;
    }
    return (uint8_t)pct;
}

static void Menu_NormalizeThresholds(void)
{
    g_threshold.temp_low = Menu_ClampI32(g_threshold.temp_low, TEMP_MIN_VALUE, TEMP_MAX_VALUE);
    g_threshold.temp_high = Menu_ClampI32(g_threshold.temp_high, TEMP_MIN_VALUE, TEMP_MAX_VALUE);
    g_threshold.humi_low = Menu_ClampI32(g_threshold.humi_low, HUMI_MIN_VALUE, HUMI_MAX_VALUE);
    g_threshold.humi_high = Menu_ClampI32(g_threshold.humi_high, HUMI_MIN_VALUE, HUMI_MAX_VALUE);
    g_threshold.smoke_high_pct = Menu_ClampI32(g_threshold.smoke_high_pct, PCT_MIN_VALUE, PCT_MAX_VALUE);

    if (g_threshold.temp_low >= g_threshold.temp_high)
    {
        if (g_threshold.temp_low < TEMP_MAX_VALUE)
        {
            g_threshold.temp_high = g_threshold.temp_low + 1;
        }
        else
        {
            g_threshold.temp_low = g_threshold.temp_high - 1;
        }
    }

    if (g_threshold.humi_low >= g_threshold.humi_high)
    {
        if (g_threshold.humi_low < HUMI_MAX_VALUE)
        {
            g_threshold.humi_high = g_threshold.humi_low + 1;
        }
        else
        {
            g_threshold.humi_low = g_threshold.humi_high - 1;
        }
    }
}

static void Menu_UpdatePirSensor(void)
{
    SR602_Update();
    g_sensor.pir_ready = SR602_IsReady();
    g_sensor.pir_raw = SR602_GetRawLevel();
    g_sensor.pir_active = (g_sensor.pir_ready != 0U) ? SR602_GetLevel() : 0U;
}

static void Menu_SampleSensors(void)
{
    OneNetTelemetry_t cache;

    if (DHT11_ReadData() == 0U)
    {
        g_sensor.temperature = Menu_ClampI32((int32_t)DHT11_Data.temperature, TEMP_MIN_VALUE, CLOUD_TEMP_MAX_VALUE);
        g_sensor.humidity = Menu_ClampI32((int32_t)DHT11_Data.humidity, HUMI_MIN_VALUE, HUMI_MAX_VALUE);
        g_sensor.dht_valid = 1U;
    }
    else
    {
        g_sensor.dht_valid = 0U;
    }

    g_sensor.smoke_raw = MQ2_Smoke_GetAO_Raw();
    g_sensor.smoke_pct = Menu_RawToPercent(g_sensor.smoke_raw);
    Menu_UpdatePirSensor();
    g_sensor.update_tick = HAL_GetTick();

    /* 更新 OneNet 遥测缓存，供自动上报使用 */
    cache.av_alm = (g_alarm.output_on != 0U) ? 1U : 0U;
    cache.pir = (g_sensor.pir_active != 0U) ? 1U : 0U;
    cache.hum = Menu_ClampI32(g_sensor.humidity, HUMI_MIN_VALUE, HUMI_MAX_VALUE);
    cache.smoke = Menu_ClampI32((int32_t)g_sensor.smoke_pct, PCT_MIN_VALUE, PCT_MAX_VALUE);
    cache.temp = Menu_ClampI32(g_sensor.temperature, TEMP_MIN_VALUE, CLOUD_TEMP_MAX_VALUE);
    OneNet_UpdateTelemetryCache(&cache);
}

static void Menu_UpdateHighTriggerI32(uint8_t *state, int32_t value, int32_t threshold, int32_t hysteresis)
{
    if (*state != 0U)
    {
        if (value < (threshold - hysteresis))
        {
            *state = 0U;
        }
    }
    else
    {
        if (value > threshold)
        {
            *state = 1U;
        }
    }
}

static void Menu_UpdateLowTriggerI32(uint8_t *state, int32_t value, int32_t threshold, int32_t hysteresis)
{
    if (*state != 0U)
    {
        if (value > (threshold + hysteresis))
        {
            *state = 0U;
        }
    }
    else
    {
        if (value < threshold)
        {
            *state = 1U;
        }
    }
}

static void Menu_UpdateHighTriggerU8(uint8_t *state, uint8_t value, int32_t threshold, int32_t hysteresis)
{
    int32_t current = (int32_t)value;
    if (*state != 0U)
    {
        if (current < (threshold - hysteresis))
        {
            *state = 0U;
        }
    }
    else
    {
        if (current > threshold)
        {
            *state = 1U;
        }
    }
}

static void Menu_UpdateAutoAlarm(void)
{
    uint8_t reason_flags = 0U;

    if (g_sensor.dht_valid != 0U)
    {
        Menu_UpdateLowTriggerI32(&g_alarm.temp_low_active, g_sensor.temperature, g_threshold.temp_low, TEMP_HYSTERESIS);
        Menu_UpdateHighTriggerI32(&g_alarm.temp_high_active, g_sensor.temperature, g_threshold.temp_high, TEMP_HYSTERESIS);
        Menu_UpdateLowTriggerI32(&g_alarm.humi_low_active, g_sensor.humidity, g_threshold.humi_low, HUMI_HYSTERESIS);
        Menu_UpdateHighTriggerI32(&g_alarm.humi_high_active, g_sensor.humidity, g_threshold.humi_high, HUMI_HYSTERESIS);
    }

    Menu_UpdateHighTriggerU8(&g_alarm.smoke_high_active, g_sensor.smoke_pct, g_threshold.smoke_high_pct, PCT_HYSTERESIS);
    g_alarm.pir_intrusion_active = ((g_security_mode == SECURITY_MODE_ARMED) && (g_sensor.pir_active != 0U)) ? 1U : 0U;

    if (g_alarm.temp_low_active != 0U)
    {
        reason_flags |= ALARM_REASON_TEMP_LOW;
    }
    if (g_alarm.temp_high_active != 0U)
    {
        reason_flags |= ALARM_REASON_TEMP_HIGH;
    }
    if (g_alarm.humi_low_active != 0U)
    {
        reason_flags |= ALARM_REASON_HUMI_LOW;
    }
    if (g_alarm.humi_high_active != 0U)
    {
        reason_flags |= ALARM_REASON_HUMI_HIGH;
    }
    if (g_alarm.smoke_high_active != 0U)
    {
        reason_flags |= ALARM_REASON_SMOKE_HIGH;
    }
    if (g_alarm.pir_intrusion_active != 0U)
    {
        reason_flags |= ALARM_REASON_PIR_INTRUSION;
    }

    g_alarm.reason_flags = reason_flags;
    g_alarm.auto_alarm = (reason_flags != 0U) ? 1U : 0U;
}

static void Menu_ApplyAlarmOutput(void)
{
    uint8_t output = 0U;

    if (g_alarm.mode == ALARM_MODE_FORCE_ON)
    {
        output = 1U;
    }
    else if (g_alarm.mode == ALARM_MODE_FORCE_OFF)
    {
        output = 0U;
    }
    else
    {
        output = g_alarm.auto_alarm;
    }

    g_alarm.output_on = output;

    if (output != 0U)
    {
        AV_Alarm_On();
    }
    else
    {
        AV_Alarm_Off();
    }
}

static void Menu_SetAlarmMode(AlarmMode_t mode, ControlSource_t source)
{
    g_alarm.mode = mode;
    g_alarm.control_source = source;
    g_alarm.last_control_tick = HAL_GetTick();

    if (mode == ALARM_MODE_AUTO)
    {
        g_manual_mode = 0U;
        g_manual_alarm_on = 0U;
    }
    else
    {
        g_manual_mode = 1U;
        g_manual_alarm_on = (mode == ALARM_MODE_FORCE_ON) ? 1U : 0U;
    }

    Menu_ApplyAlarmOutput();
}

void Menu_SetRemoteAlarm(uint8_t alarm_on)
{
    Menu_SetAlarmMode((alarm_on != 0U) ? ALARM_MODE_FORCE_ON : ALARM_MODE_FORCE_OFF, CONTROL_SRC_REMOTE);
    Menu_HistoryAdd((alarm_on != 0U) ? "Remote Alarm ON" : "Remote Alarm OFF");
    Menu_RequestTelemetryReport(REMOTE_REPORT_DELAY_MS);
}

static void Menu_SetSecurityMode(SecurityMode_t mode)
{
    if (g_security_mode != mode)
    {
        g_security_mode = mode;
        Menu_HistoryAdd((mode == SECURITY_MODE_ARMED) ? "Security Armed" : "Security Normal");
    }

    Menu_UpdateAutoAlarm();
    Menu_ApplyAlarmOutput();
    Menu_RequestTelemetryReport(REMOTE_REPORT_DELAY_MS);
}

static void Menu_StrTrim(char *text)
{
    char *start = text;
    size_t len;

    if (text == NULL)
    {
        return;
    }

    while ((*start == ' ') || (*start == '\t') || (*start == '\r') || (*start == '\n'))
    {
        start++;
    }

    if (start != text)
    {
        memmove(text, start, strlen(start) + 1U);
    }

    len = strlen(text);
    while (len > 0U)
    {
        char ch = text[len - 1U];
        if ((ch == ' ') || (ch == '\t') || (ch == '\r') || (ch == '\n'))
        {
            text[len - 1U] = '\0';
            len--;
        }
        else
        {
            break;
        }
    }
}

static void Menu_StrToUpper(char *text)
{
    uint16_t i = 0U;

    if (text == NULL)
    {
        return;
    }

    while (text[i] != '\0')
    {
        if ((text[i] >= 'a') && (text[i] <= 'z'))
        {
            text[i] = (char)(text[i] - 'a' + 'A');
        }
        i++;
    }
}

static uint8_t Menu_ParseI32(const char *text, int32_t *out_value)
{
    char *end_ptr = NULL;
    long value = 0;

    if ((text == NULL) || (out_value == NULL))
    {
        return 0U;
    }

    value = strtol(text, &end_ptr, 10);
    if (end_ptr == text)
    {
        return 0U;
    }

    while ((*end_ptr == ' ') || (*end_ptr == '\t'))
    {
        end_ptr++;
    }

    if (*end_ptr != '\0')
    {
        return 0U;
    }

    *out_value = (int32_t)value;
    return 1U;
}

static void Menu_HistoryAdd(const char *line)
{
    if ((line == NULL) || (line[0] == '\0'))
    {
        return;
    }

    strncpy(g_history.items[g_history.head], line, HISTORY_TEXT_LEN - 1U);
    g_history.items[g_history.head][HISTORY_TEXT_LEN - 1U] = '\0';

    g_history.head = (uint8_t)((g_history.head + 1U) % HISTORY_SIZE);
    if (g_history.count < HISTORY_SIZE)
    {
        g_history.count++;
    }
}

static const char *Menu_HistoryGetNewest(uint8_t index)
{
    int16_t position = 0;

    if (index >= g_history.count)
    {
        return NULL;
    }

    position = (int16_t)g_history.head - 1 - (int16_t)index;
    while (position < 0)
    {
        position += (int16_t)HISTORY_SIZE;
    }

    return g_history.items[position];
}

static const char *Menu_AlarmModeText(AlarmMode_t mode)
{
    if (mode == ALARM_MODE_FORCE_ON)
    {
        return "FORCE_ON";
    }
    if (mode == ALARM_MODE_FORCE_OFF)
    {
        return "FORCE_OFF";
    }
    return "AUTO";
}

static const char *Menu_ControlSourceText(ControlSource_t source)
{
    if (source == CONTROL_SRC_LOCAL)
    {
        return "LOCAL";
    }
    if (source == CONTROL_SRC_REMOTE)
    {
        return "REMOTE";
    }
    return "AUTO";
}

static const char *Menu_SecurityModeText(SecurityMode_t mode)
{
    if (mode == SECURITY_MODE_ARMED)
    {
        return "ARMED";
    }

    return "NORMAL";
}

static const char *Menu_AlarmReasonText(uint8_t reason_flags)
{
    if (reason_flags == 0U)
    {
        return "NONE";
    }

    if ((reason_flags & (uint8_t)(reason_flags - 1U)) != 0U)
    {
        return "MULTI";
    }

    if (reason_flags == ALARM_REASON_TEMP_LOW)
    {
        return "TEMP_LOW";
    }
    if (reason_flags == ALARM_REASON_TEMP_HIGH)
    {
        return "TEMP_HIGH";
    }
    if (reason_flags == ALARM_REASON_HUMI_LOW)
    {
        return "HUMI_LOW";
    }
    if (reason_flags == ALARM_REASON_HUMI_HIGH)
    {
        return "HUMI_HIGH";
    }
    if (reason_flags == ALARM_REASON_SMOKE_HIGH)
    {
        return "SMOKE_HIGH";
    }
    if (reason_flags == ALARM_REASON_PIR_INTRUSION)
    {
        return "PIR_INTRUSION";
    }

    return "UNKNOWN";
}

static void Menu_FormatDataMessage(char *buffer, uint16_t size, uint8_t with_ok_prefix)
{
    if (with_ok_prefix != 0U)
    {
        snprintf(buffer, size,
                 "OK DATA T=%ld H=%ld S=%u PIR=%u AO=%u AM=%s AS=%s SEC=%s R=%s\r\n",
                 (long)g_sensor.temperature, (long)g_sensor.humidity,
                 (unsigned int)g_sensor.smoke_pct, (unsigned int)g_sensor.pir_active,
                 (unsigned int)g_alarm.output_on,
                 Menu_AlarmModeText(g_alarm.mode),
                 Menu_ControlSourceText(g_alarm.control_source),
                 Menu_SecurityModeText(g_security_mode),
                 Menu_AlarmReasonText(g_alarm.reason_flags));
    }
    else
    {
        snprintf(buffer, size,
                 "DATA T=%ld H=%ld S=%u PIR=%u AO=%u AM=%s AS=%s SEC=%s R=%s\r\n",
                 (long)g_sensor.temperature, (long)g_sensor.humidity,
                 (unsigned int)g_sensor.smoke_pct, (unsigned int)g_sensor.pir_active,
                 (unsigned int)g_alarm.output_on,
                 Menu_AlarmModeText(g_alarm.mode),
                 Menu_ControlSourceText(g_alarm.control_source),
                 Menu_SecurityModeText(g_security_mode),
                 Menu_AlarmReasonText(g_alarm.reason_flags));
    }
}

static void Menu_RequestTelemetryReport(uint32_t delay_ms)
{
    g_report_pending = 1U;
    g_report_due_tick = HAL_GetTick() + delay_ms;
}

static uint8_t Menu_SendPeriodicData(void)
{
    OneNetTelemetry_t telemetry;
    char history_line[HISTORY_TEXT_LEN];

    if (OneNet_IsConnected() == 0U)
    {
        return 0U;
    }

    telemetry.av_alm = (g_alarm.output_on != 0U) ? 1U : 0U;
    telemetry.pir = (g_sensor.pir_active != 0U) ? 1U : 0U;
    telemetry.hum = Menu_ClampI32(g_sensor.humidity, HUMI_MIN_VALUE, HUMI_MAX_VALUE);
    telemetry.smoke = Menu_ClampI32((int32_t)g_sensor.smoke_pct, PCT_MIN_VALUE, PCT_MAX_VALUE);
    telemetry.temp = Menu_ClampI32(g_sensor.temperature, TEMP_MIN_VALUE, CLOUD_TEMP_MAX_VALUE);

    if (OneNet_PublishTelemetry(&telemetry) != 0U)
    {
        snprintf(history_line,
                 sizeof(history_line),
                 "T=%ld H=%ld S=%ld PIR=%u A=%u",
                 (long)telemetry.temp,
                 (long)telemetry.hum,
                 (long)telemetry.smoke,
                 (unsigned int)telemetry.pir,
                 (unsigned int)telemetry.av_alm);
        Menu_HistoryAdd(history_line);
        return 1U;
    }

    return 0U;
}

static void Menu_SendOk(uint8_t link_id, const char *message)
{
    char tx_buf[ESP_TX_BUF_SIZE];

    if (message == NULL)
    {
        snprintf(tx_buf, sizeof(tx_buf), "OK\r\n");
    }
    else
    {
        snprintf(tx_buf, sizeof(tx_buf), "OK %s\r\n", message);
    }
    ESP_SendString(link_id, tx_buf);
}

static void Menu_SendErr(uint8_t link_id, const char *code, const char *message)
{
    char tx_buf[ESP_TX_BUF_SIZE];

    snprintf(tx_buf, sizeof(tx_buf), "ERR %s %s\r\n", code, message);
    ESP_SendString(link_id, tx_buf);
}

static void Menu_ProcessCommandLine(uint8_t link_id, char *line)
{
    char cmd_buf[ESP_DATA_BUF_SIZE + 1U];
    char history_line[HISTORY_TEXT_LEN];
    char tx_buf[ESP_TX_BUF_SIZE];
    char *key = NULL;
    char *value = NULL;
    char *eq = NULL;
    int32_t number = 0;

    if (line == NULL)
    {
        return;
    }

    strncpy(cmd_buf, line, sizeof(cmd_buf) - 1U);
    cmd_buf[sizeof(cmd_buf) - 1U] = '\0';
    Menu_StrTrim(cmd_buf);

    if (cmd_buf[0] == '\0')
    {
        return;
    }

    strncpy(history_line, cmd_buf, sizeof(history_line) - 1U);
    history_line[sizeof(history_line) - 1U] = '\0';
    Menu_HistoryAdd(history_line);

    Menu_StrToUpper(cmd_buf);

    if (strcmp(cmd_buf, "GET ALL") == 0)
    {
        Menu_FormatDataMessage(tx_buf, sizeof(tx_buf), 1U);
        ESP_SendString(link_id, tx_buf);
        return;
    }

    if (strcmp(cmd_buf, "GET THRESHOLD") == 0)
    {
        snprintf(tx_buf, sizeof(tx_buf),
                 "OK THR TL=%ld TH=%ld HL=%ld HH=%ld SH=%ld\r\n",
                 (long)g_threshold.temp_low, (long)g_threshold.temp_high,
                 (long)g_threshold.humi_low, (long)g_threshold.humi_high,
                 (long)g_threshold.smoke_high_pct);
        ESP_SendString(link_id, tx_buf);
        return;
    }

    if (strcmp(cmd_buf, "GET ALARM") == 0)
    {
        snprintf(tx_buf, sizeof(tx_buf),
                 "OK ALARM AO=%u AM=%s AS=%s SEC=%s R=%s\r\n",
                 (unsigned int)g_alarm.output_on,
                 Menu_AlarmModeText(g_alarm.mode),
                 Menu_ControlSourceText(g_alarm.control_source),
                 Menu_SecurityModeText(g_security_mode),
                 Menu_AlarmReasonText(g_alarm.reason_flags));
        ESP_SendString(link_id, tx_buf);
        return;
    }

    if (strncmp(cmd_buf, "SET ", 4U) != 0)
    {
        Menu_SendErr(link_id, "400", "BAD_CMD");
        return;
    }

    key = cmd_buf + 4;
    eq = strchr(key, '=');
    if (eq == NULL)
    {
        Menu_SendErr(link_id, "400", "BAD_SET_FORMAT");
        return;
    }

    *eq = '\0';
    value = eq + 1;
    Menu_StrTrim(key);
    Menu_StrTrim(value);

    if (strcmp(key, "ALARM_MODE") == 0)
    {
        if (strcmp(value, "AUTO") == 0)
        {
            Menu_SetAlarmMode(ALARM_MODE_AUTO, CONTROL_SRC_REMOTE);
            Menu_SendOk(link_id, "ALARM_MODE=AUTO");
            return;
        }
        if (strcmp(value, "FORCE_ON") == 0)
        {
            Menu_SetAlarmMode(ALARM_MODE_FORCE_ON, CONTROL_SRC_REMOTE);
            Menu_SendOk(link_id, "ALARM_MODE=FORCE_ON");
            return;
        }
        if (strcmp(value, "FORCE_OFF") == 0)
        {
            Menu_SetAlarmMode(ALARM_MODE_FORCE_OFF, CONTROL_SRC_REMOTE);
            Menu_SendOk(link_id, "ALARM_MODE=FORCE_OFF");
            return;
        }

        Menu_SendErr(link_id, "422", "ALARM_MODE_INVALID");
        return;
    }

    if (strcmp(key, "CURSOR") == 0)
    {
        if (strcmp(value, "REVERSE") == 0)
        {
            MF_SetCursorStyle(MF_CURSOR_REVERSE);
            Menu_SendOk(link_id, "CURSOR=REVERSE");
            return;
        }
        if (strcmp(value, "BOX") == 0)
        {
            MF_SetCursorStyle(MF_CURSOR_BOX);
            Menu_SendOk(link_id, "CURSOR=BOX");
            return;
        }

        Menu_SendErr(link_id, "422", "CURSOR_INVALID");
        return;
    }

    if (Menu_ParseI32(value, &number) == 0U)
    {
        Menu_SendErr(link_id, "422", "BAD_VALUE");
        return;
    }

    if (strcmp(key, "TEMP_LOW") == 0)
    {
        g_threshold.temp_low = number;
    }
    else if (strcmp(key, "TEMP_HIGH") == 0)
    {
        g_threshold.temp_high = number;
    }
    else if (strcmp(key, "HUMI_LOW") == 0)
    {
        g_threshold.humi_low = number;
    }
    else if (strcmp(key, "HUMI_HIGH") == 0)
    {
        g_threshold.humi_high = number;
    }
    else if (strcmp(key, "SMOKE_HIGH") == 0)
    {
        g_threshold.smoke_high_pct = number;
    }
    else
    {
        Menu_SendErr(link_id, "400", "UNKNOWN_KEY");
        return;
    }

    Menu_NormalizeThresholds();
    Menu_UpdateAutoAlarm();
    Menu_ApplyAlarmOutput();

    Menu_SendOk(link_id, "SET_DONE");
}

static void Menu_ActionCursorReverse(void)
{
    MF_SetCursorStyle(MF_CURSOR_REVERSE);
}

static void Menu_ActionCursorBox(void)
{
    MF_SetCursorStyle(MF_CURSOR_BOX);
}

static void Menu_ActionSecurityNormal(void)
{
    Menu_SetSecurityMode(SECURITY_MODE_NORMAL);
}

static void Menu_ActionSecurityArmed(void)
{
    Menu_SetSecurityMode(SECURITY_MODE_ARMED);
}

static void Menu_OnThresholdChanged(int32_t value)
{
    (void)value;
    Menu_NormalizeThresholds();
    Menu_UpdateAutoAlarm();
    Menu_ApplyAlarmOutput();
}

static void Menu_OnManualModeChanged(void)
{
    if (g_manual_mode == 0U)
    {
        Menu_SetAlarmMode(ALARM_MODE_AUTO, CONTROL_SRC_LOCAL);
    }
    else
    {
        if (g_manual_alarm_on != 0U)
        {
            Menu_SetAlarmMode(ALARM_MODE_FORCE_ON, CONTROL_SRC_LOCAL);
        }
        else
        {
            Menu_SetAlarmMode(ALARM_MODE_FORCE_OFF, CONTROL_SRC_LOCAL);
        }
    }
}

static void Menu_OnManualAlarmChanged(void)
{
    if (g_manual_mode == 0U)
    {
        g_manual_alarm_on = (g_alarm.mode == ALARM_MODE_FORCE_ON) ? 1U : 0U;
        return;
    }

    if (g_manual_alarm_on != 0U)
    {
        Menu_SetAlarmMode(ALARM_MODE_FORCE_ON, CONTROL_SRC_LOCAL);
    }
    else
    {
        Menu_SetAlarmMode(ALARM_MODE_FORCE_OFF, CONTROL_SRC_LOCAL);
    }
}

static void Menu_DrawPageHeader(const char *title)
{
    OLED_ShowString(0, 0, (char *)title, OLED_8X16);
    OLED_DrawLine(0, 16, 128, 16);
}

static void Menu_PageTempHumi(KeyEvent_t key, uint8_t *exit_flag)
{
    char line[32];

    if (key == KEY_BACK)
    {
        *exit_flag = 1U;
        return;
    }

    Menu_DrawPageHeader("Temp/Humi");

    if (g_sensor.dht_valid != 0U)
    {
        snprintf(line, sizeof(line), "Temp:%ldC", (long)g_sensor.temperature);
        OLED_ShowString(0, 22, line, OLED_8X16);

        snprintf(line, sizeof(line), "Humi:%ld%%", (long)g_sensor.humidity);
        OLED_ShowString(0, 40, line, OLED_8X16);
    }
    else
    {
        OLED_ShowString(0, 26, "DHT11 Read Failed", OLED_6X8);
    }
}

static void Menu_DrawPercentBar(uint8_t percent, uint8_t top_y)
{
    uint8_t filled = (uint8_t)(((uint16_t)percent * 118U) / 100U);

    OLED_DrawRectangle(4, top_y, 120, 12, OLED_UNFILLED);
    if (filled > 0U)
    {
        OLED_DrawRectangle(5, top_y + 1U, filled, 10, OLED_FILLED);
    }
}

static void Menu_PagePIR(KeyEvent_t key, uint8_t *exit_flag)
{
    char line[32];

    if (key == KEY_BACK)
    {
        *exit_flag = 1U;
        return;
    }

    Menu_DrawPageHeader("PIR Sensor");

    snprintf(line, sizeof(line), "State:%s", (g_sensor.pir_active != 0U) ? "Motion" : "Normal");
    OLED_ShowString(0, 18, line, OLED_6X8);

    snprintf(line, sizeof(line), "Ready:%s", (g_sensor.pir_ready != 0U) ? "YES" : "WARMUP");
    OLED_ShowString(0, 27, line, OLED_6X8);

    snprintf(line, sizeof(line), "Mode:%s", Menu_SecurityModeText(g_security_mode));
    OLED_ShowString(0, 36, line, OLED_6X8);

    snprintf(line, sizeof(line), "Raw:%u", (unsigned int)g_sensor.pir_raw);
    OLED_ShowString(0, 45, line, OLED_6X8);
}

static void Menu_PageSmoke(KeyEvent_t key, uint8_t *exit_flag)
{
    char line[32];

    if (key == KEY_BACK)
    {
        *exit_flag = 1U;
        return;
    }

    Menu_DrawPageHeader("Smoke Sensor");

    snprintf(line, sizeof(line), "Value:%u%%", (unsigned int)g_sensor.smoke_pct);
    OLED_ShowString(0, 18, line, OLED_6X8);

    snprintf(line, sizeof(line), "ADC Raw:%u", (unsigned int)g_sensor.smoke_raw);
    OLED_ShowString(0, 27, line, OLED_6X8);

    snprintf(line, sizeof(line), "Thr:%ld%%", (long)g_threshold.smoke_high_pct);
    OLED_ShowString(0, 36, line, OLED_6X8);

    Menu_DrawPercentBar(g_sensor.smoke_pct, 50);
}

static void Menu_PageHistory(KeyEvent_t key, uint8_t *exit_flag)
{
    uint8_t i = 0U;
    char line[32];

    if (key == KEY_BACK)
    {
        *exit_flag = 1U;
        return;
    }

    Menu_DrawPageHeader("Cloud History");

    if (g_history.count == 0U)
    {
        OLED_ShowString(0, 28, "No Upload Yet", OLED_6X8);
    }
    else
    {
        for (i = 0U; i < HISTORY_SIZE; i++)
        {
            const char *entry = Menu_HistoryGetNewest(i);
            char preview[21];
            if (entry == NULL)
            {
                break;
            }

            strncpy(preview, entry, sizeof(preview) - 1U);
            preview[sizeof(preview) - 1U] = '\0';
            snprintf(line, sizeof(line), "%u:%s", (unsigned int)(i + 1U), preview);
            OLED_ShowString(0, (int16_t)(18 + i * 9), line, OLED_6X8);
        }
    }
}

static void Menu_PageWiFiDebug(KeyEvent_t key, uint8_t *exit_flag)
{
    char line[32];

    if (key == KEY_BACK)
    {
        *exit_flag = 1U;
        return;
    }

    Menu_DrawPageHeader("WiFi Debug");

    snprintf(line, sizeof(line), "SSID:%s", ESP_WIFI_SSID);
    OLED_ShowString(0, 18, line, OLED_6X8);

    snprintf(line, sizeof(line), "WiFi:%s", (ESP_IsWifiConnected() != 0U) ? "OK" : "WAIT");
    OLED_ShowString(0, 27, line, OLED_6X8);

    snprintf(line, sizeof(line), "TCP :%s", (ESP_IsConnected() != 0U) ? "OK" : "WAIT");
    OLED_ShowString(0, 36, line, OLED_6X8);

    snprintf(line, sizeof(line), "Cloud:%s", (OneNet_IsConnected() != 0U) ? "OK" : "WAIT");
    OLED_ShowString(0, 45, line, OLED_6X8);

    snprintf(line, sizeof(line), "Upload:%lds", (long)g_report_period_sec);
    OLED_ShowString(0, 54, line, OLED_6X8);
}

static void Menu_PageAlarmStatus(KeyEvent_t key, uint8_t *exit_flag)
{
    char line[32];

    if (key == KEY_BACK)
    {
        *exit_flag = 1U;
        return;
    }

    Menu_DrawPageHeader("Alarm Status");

    snprintf(line, sizeof(line), "Out:%s", (g_alarm.output_on != 0U) ? "ON" : "OFF");
    OLED_ShowString(0, 18, line, OLED_6X8);

    snprintf(line, sizeof(line), "Mode:%s", Menu_AlarmModeText(g_alarm.mode));
    OLED_ShowString(0, 27, line, OLED_6X8);

    snprintf(line, sizeof(line), "Src:%s", Menu_ControlSourceText(g_alarm.control_source));
    OLED_ShowString(0, 36, line, OLED_6X8);

    snprintf(line, sizeof(line), "Sec:%s", Menu_SecurityModeText(g_security_mode));
    OLED_ShowString(0, 45, line, OLED_6X8);

    snprintf(line, sizeof(line), "Reason:%s", Menu_AlarmReasonText(g_alarm.reason_flags));
    OLED_ShowString(0, 54, line, OLED_6X8);
}

static void Menu_BuildMenus(void)
{
    g_root_menu = MF_CreateMenu("Lab Alarm");
    g_menu_sensor = MF_CreateMenu("Sensor Data");
    g_menu_threshold = MF_CreateMenu("Threshold");
    g_menu_history = MF_CreateMenu("WIFI");
    g_menu_cursor = MF_CreateMenu("Cursor");
    g_menu_alarm = MF_CreateMenu("Alarm Ctrl");
    g_menu_security = MF_CreateMenu("Security Mode");

    if ((g_root_menu == NULL) || (g_menu_sensor == NULL) || (g_menu_threshold == NULL) ||
        (g_menu_history == NULL) || (g_menu_cursor == NULL) || (g_menu_alarm == NULL) ||
        (g_menu_security == NULL))
    {
        return;
    }

    MF_AddSubmenu(g_root_menu, "1.View Sensor", g_menu_sensor);
    MF_AddSubmenu(g_root_menu, "2.Set Threshold", g_menu_threshold);
    MF_AddSubmenu(g_root_menu, "3.WIFI", g_menu_history);
    MF_AddSubmenu(g_root_menu, "4.Cursor Style", g_menu_cursor);
    MF_AddSubmenu(g_root_menu, "5.Alarm Control", g_menu_alarm);
    MF_AddSubmenu(g_root_menu, "6.Security Mode", g_menu_security);

    MF_AddCustomPage(g_menu_sensor, "Temp/Humi", Menu_PageTempHumi);
    MF_AddCustomPage(g_menu_sensor, "PIR", Menu_PagePIR);
    MF_AddCustomPage(g_menu_sensor, "Smoke", Menu_PageSmoke);

    MF_AddValue(g_menu_threshold, "Temp Low", &g_threshold.temp_low, TEMP_MIN_VALUE, TEMP_MAX_VALUE, 1, "C", Menu_OnThresholdChanged);
    MF_AddValue(g_menu_threshold, "Temp High", &g_threshold.temp_high, TEMP_MIN_VALUE, TEMP_MAX_VALUE, 1, "C", Menu_OnThresholdChanged);
    MF_AddValue(g_menu_threshold, "Humi Low", &g_threshold.humi_low, HUMI_MIN_VALUE, HUMI_MAX_VALUE, 1, "%", Menu_OnThresholdChanged);
    MF_AddValue(g_menu_threshold, "Humi High", &g_threshold.humi_high, HUMI_MIN_VALUE, HUMI_MAX_VALUE, 1, "%", Menu_OnThresholdChanged);
    MF_AddValue(g_menu_threshold, "Smoke High", &g_threshold.smoke_high_pct, PCT_MIN_VALUE, PCT_MAX_VALUE, 1, "%", Menu_OnThresholdChanged);

    MF_AddCustomPage(g_menu_history, "Last 5 Uploads", Menu_PageHistory);
    MF_AddCustomPage(g_menu_history, "WiFi Debug", Menu_PageWiFiDebug);

    MF_AddAction(g_menu_cursor, "Reverse", Menu_ActionCursorReverse);
    MF_AddAction(g_menu_cursor, "Box", Menu_ActionCursorBox);

    MF_AddCustomPage(g_menu_alarm, "Current Status", Menu_PageAlarmStatus);
    MF_AddToggle(g_menu_alarm, "Manual Ctrl", &g_manual_mode, Menu_OnManualModeChanged);
    MF_AddToggle(g_menu_alarm, "Manual Alarm", &g_manual_alarm_on, Menu_OnManualAlarmChanged);

    MF_AddAction(g_menu_security, "Normal", Menu_ActionSecurityNormal);
    MF_AddAction(g_menu_security, "Armed", Menu_ActionSecurityArmed);
}

void Menu_SystemInit(void)
{
    memset(&g_history, 0, sizeof(g_history));
    memset(g_known_link, 0, sizeof(g_known_link));

    OLED_Init();
    Key_Init();
    AV_Alarm_Init();
    SR602_Init();
    MQ2_Smoke_Init();
    (void)DHT11_Init();

    Menu_NormalizeThresholds();
    Menu_BuildMenus();

    MF_SetCursorStyle(MF_CURSOR_REVERSE);
    if (g_root_menu != NULL)
    {
        MF_Start(g_root_menu);
    }

    OneNet_RegisterAlarmControlCallback(Menu_SetRemoteAlarm);
    if (OneNet_Init() == 0U)
    {
        Menu_HistoryAdd("Cloud Init Fail");
    }

    OneNet_SetReportInterval(0U);

    Menu_SampleSensors();
    Menu_UpdateAutoAlarm();
    Menu_ApplyAlarmOutput();

    g_last_sample_tick = HAL_GetTick();
    g_last_report_tick = HAL_GetTick();
    g_report_pending = 0U;
    g_report_due_tick = 0U;
}

void Menu_SystemLoop(void)
{
    uint32_t now = HAL_GetTick();

    Menu_UpdatePirSensor();
    Menu_UpdateAutoAlarm();
    Menu_ApplyAlarmOutput();

    OneNet_Process();

    if ((now - g_last_sample_tick) >= SENSOR_SAMPLE_PERIOD_MS)
    {
        g_last_sample_tick = now;
        Menu_SampleSensors();
        Menu_UpdateAutoAlarm();
        Menu_ApplyAlarmOutput();
    }

    if ((g_report_pending != 0U) && ((int32_t)(now - g_report_due_tick) >= 0))
    {
        if (Menu_SendPeriodicData() != 0U)
        {
            g_report_pending = 0U;
        }
        g_last_report_tick = now;
    }
    else if ((now - g_last_report_tick) >= ((uint32_t)g_report_period_sec * 1000U))
    {
        g_last_report_tick = now;
        (void)Menu_SendPeriodicData();
    }

    MF_Loop();
}
