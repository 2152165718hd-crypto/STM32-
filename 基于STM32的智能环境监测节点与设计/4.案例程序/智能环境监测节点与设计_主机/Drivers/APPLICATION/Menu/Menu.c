#include ".\Application\Menu\Menu.h"
#include ".\APPLICATION\EnvProto\EnvProto.h"
#include ".\APPLICATION\Menu\MenuFramework.h"
#include ".\Hardware\Buzzer\Buzzer.h"
#include ".\Hardware\KEY\KEY.h"
#include ".\Hardware\OLED\OLED.h"
#include ".\Hardware\Zigbee\Zigbee.h"
#include <stdio.h>
#include <string.h>

#define HOST_COMM_TIMEOUT_MS      3000U
#define HOST_QUERY_INTERVAL_MS    1000U

#define HOST_ALARM_PM25           0x01U
#define HOST_ALARM_MQ135          0x02U
#define HOST_ALARM_LIGHT          0x04U
#define HOST_ALARM_TEMP           0x08U
#define HOST_ALARM_HUMI           0x10U

static HostSensorCache_t s_sensor_cache;
static HostThresholds_t s_thresholds;
static HostAlarmState_t s_alarm_state;

static MF_Menu_t *s_root_menu = NULL;
static MF_Menu_t *s_sensor_menu = NULL;
static MF_Menu_t *s_threshold_menu = NULL;

static uint32_t s_last_query_tick = 0U;

static uint8_t Host_GetMq135Percent(uint16_t mq135_mv)
{
    uint32_t percent = ((uint32_t)mq135_mv * 100U + 2500U) / 5000U;

    if (percent > 100U)
    {
        percent = 100U;
    }

    return (uint8_t)percent;
}

static int32_t Host_ClampThreshold(int32_t value, int32_t min_value, int32_t max_value)
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

static uint8_t Host_IsFresh(uint8_t valid_bit)
{
    if ((s_sensor_cache.has_report == 0U) || (s_sensor_cache.is_stale != 0U))
    {
        return 0U;
    }

    return ((s_sensor_cache.valid_bits & valid_bit) != 0U) ? 1U : 0U;
}

static void Host_DrawTitle(const char *title)
{
    OLED_ShowString(0, 0, (char *)title, OLED_6X8);
    OLED_DrawLine(0, 10, 127, 10);
}

static void Host_SendQueryNow(void)
{
    uint8_t frame_buf[8];
    uint8_t frame_len = 0U;

    if (EnvProto_BuildQueryNow(frame_buf, &frame_len) != 0U)
    {
        (void)Zigbee_SendFrame(frame_buf, frame_len);
        s_last_query_tick = HAL_GetTick();
    }
}

static void Host_UpdateFromReport(const EnvProto_SensorPayload_t *payload)
{
    if (payload == NULL)
    {
        return;
    }

    s_sensor_cache.valid_bits = payload->valid_bits;
    s_sensor_cache.temp_c = payload->temp_c;
    s_sensor_cache.hum_pct = payload->hum_pct;
    s_sensor_cache.pm25_ugm3 = payload->pm25_ugm3;
    s_sensor_cache.mq135_mv = payload->mq135_mv;
    s_sensor_cache.light_lux = payload->light_lux;
    s_sensor_cache.last_rx_tick = HAL_GetTick();
    s_sensor_cache.has_report = 1U;
    s_sensor_cache.is_stale = 0U;
}

static void Host_ProcessFrames(void)
{
    EnvProto_Frame_t frame;
    EnvProto_SensorPayload_t payload;

    while (Zigbee_GetFrame(&frame) != 0U)
    {
        if ((frame.type == ENV_PROTO_TYPE_SENSOR_REPORT) &&
            (EnvProto_DecodeSensorReport(&frame, &payload) != 0U))
        {
            Host_UpdateFromReport(&payload);
        }
    }
}

static void Host_UpdateLinkState(void)
{
    uint32_t now = HAL_GetTick();

    if ((s_sensor_cache.has_report == 0U) ||
        ((now - s_sensor_cache.last_rx_tick) > HOST_COMM_TIMEOUT_MS))
    {
        s_sensor_cache.is_stale = 1U;
        if ((now - s_last_query_tick) >= HOST_QUERY_INTERVAL_MS)
        {
            Host_SendQueryNow();
        }
    }
    else
    {
        s_sensor_cache.is_stale = 0U;
    }
}

static uint8_t Host_CalcAlarmMask(void)
{
    uint8_t alarm_mask = 0U;

    if ((s_sensor_cache.has_report == 0U) || (s_sensor_cache.is_stale != 0U))
    {
        return 0U;
    }

    if (Host_IsFresh(ENV_PROTO_VALID_PM25) && ((int32_t)s_sensor_cache.pm25_ugm3 > s_thresholds.pm25))
    {
        alarm_mask |= HOST_ALARM_PM25;
    }

    if (Host_IsFresh(ENV_PROTO_VALID_MQ135) &&
        ((int32_t)Host_GetMq135Percent(s_sensor_cache.mq135_mv) > s_thresholds.mq135_pct))
    {
        alarm_mask |= HOST_ALARM_MQ135;
    }

    if (Host_IsFresh(ENV_PROTO_VALID_BH1750) && ((int32_t)s_sensor_cache.light_lux > s_thresholds.light_lux))
    {
        alarm_mask |= HOST_ALARM_LIGHT;
    }

    if (Host_IsFresh(ENV_PROTO_VALID_DHT11) && ((int32_t)s_sensor_cache.temp_c > s_thresholds.temp_c))
    {
        alarm_mask |= HOST_ALARM_TEMP;
    }

    if (Host_IsFresh(ENV_PROTO_VALID_DHT11) && ((int32_t)s_sensor_cache.hum_pct > s_thresholds.hum_pct))
    {
        alarm_mask |= HOST_ALARM_HUMI;
    }

    return alarm_mask;
}

static void Host_AlarmPage(KeyEvent_t key, uint8_t *exit_flag)
{
    const char *alarm_texts[5];
    uint8_t count = 0U;
    uint8_t index = 0U;
    uint8_t start = 0U;

    if ((key == KEY_BACK) && (exit_flag != NULL))
    {
        *exit_flag = 1U;
        return;
    }

    Host_DrawTitle("Alarm Info");

    if ((s_alarm_state.active_mask & HOST_ALARM_PM25) != 0U)
    {
        alarm_texts[count++] = "PM2.5 High";
    }
    if ((s_alarm_state.active_mask & HOST_ALARM_MQ135) != 0U)
    {
        alarm_texts[count++] = "MQ135 High";
    }
    if ((s_alarm_state.active_mask & HOST_ALARM_LIGHT) != 0U)
    {
        alarm_texts[count++] = "Light High";
    }
    if ((s_alarm_state.active_mask & HOST_ALARM_TEMP) != 0U)
    {
        alarm_texts[count++] = "Temp High";
    }
    if ((s_alarm_state.active_mask & HOST_ALARM_HUMI) != 0U)
    {
        alarm_texts[count++] = "Humi High";
    }

    if (count == 0U)
    {
        OLED_Printf(0, 20, OLED_6X8, "No Alarm");
        OLED_Printf(0, 40, OLED_6X8, "Link:%s", (s_sensor_cache.is_stale != 0U) ? "STALE" : "OK");
        return;
    }

    if (count > 3U)
    {
        start = (uint8_t)((HAL_GetTick() / 1200U) % count);
    }

    for (index = 0U; index < 3U && index < count; index++)
    {
        OLED_Printf(0, 16 + (int16_t)(index * 16), OLED_6X8, "%s", alarm_texts[(start + index) % count]);
    }
}

static void Host_Pm25Page(KeyEvent_t key, uint8_t *exit_flag)
{
    if ((key == KEY_BACK) && (exit_flag != NULL))
    {
        *exit_flag = 1U;
        return;
    }

    Host_DrawTitle("PM2.5");
    if (Host_IsFresh(ENV_PROTO_VALID_PM25))
    {
        OLED_Printf(0, 16, OLED_6X8, "Value:%u ug/m3", (unsigned int)s_sensor_cache.pm25_ugm3);
    }
    else
    {
        OLED_Printf(0, 16, OLED_6X8, "Value: --");
    }
    OLED_Printf(0, 32, OLED_6X8, "Thres:%ld ug/m3", (long)s_thresholds.pm25);
    OLED_Printf(0, 48, OLED_6X8, "Alarm:%s", ((s_alarm_state.active_mask & HOST_ALARM_PM25) != 0U) ? "YES" : "NO");
}

static void Host_Mq135Page(KeyEvent_t key, uint8_t *exit_flag)
{
    uint8_t percent = Host_GetMq135Percent(s_sensor_cache.mq135_mv);

    if ((key == KEY_BACK) && (exit_flag != NULL))
    {
        *exit_flag = 1U;
        return;
    }

    Host_DrawTitle("MQ135");
    if (Host_IsFresh(ENV_PROTO_VALID_MQ135))
    {
        OLED_Printf(0, 16, OLED_6X8, "Value:%u%%", (unsigned int)percent);
        OLED_Printf(0, 32, OLED_6X8, "AO   :%u mV", (unsigned int)s_sensor_cache.mq135_mv);
    }
    else
    {
        OLED_Printf(0, 16, OLED_6X8, "Value: --");
        OLED_Printf(0, 32, OLED_6X8, "AO   : --");
    }
    OLED_Printf(0, 48, OLED_6X8, "Thres:%ld%%", (long)s_thresholds.mq135_pct);
}

static void Host_LightPage(KeyEvent_t key, uint8_t *exit_flag)
{
    if ((key == KEY_BACK) && (exit_flag != NULL))
    {
        *exit_flag = 1U;
        return;
    }

    Host_DrawTitle("Light");
    if (Host_IsFresh(ENV_PROTO_VALID_BH1750))
    {
        OLED_Printf(0, 16, OLED_6X8, "Value:%u lx", (unsigned int)s_sensor_cache.light_lux);
    }
    else
    {
        OLED_Printf(0, 16, OLED_6X8, "Value: --");
    }
    OLED_Printf(0, 32, OLED_6X8, "Thres:%ld lx", (long)s_thresholds.light_lux);
    OLED_Printf(0, 48, OLED_6X8, "Alarm:%s", ((s_alarm_state.active_mask & HOST_ALARM_LIGHT) != 0U) ? "YES" : "NO");
}

static void Host_TempHumiPage(KeyEvent_t key, uint8_t *exit_flag)
{
    if ((key == KEY_BACK) && (exit_flag != NULL))
    {
        *exit_flag = 1U;
        return;
    }

    Host_DrawTitle("Temp/Humi");
    if (Host_IsFresh(ENV_PROTO_VALID_DHT11))
    {
        OLED_Printf(0, 14, OLED_6X8, "Temp :%u C", (unsigned int)s_sensor_cache.temp_c);
        OLED_Printf(0, 26, OLED_6X8, "T Thr:%ld C", (long)s_thresholds.temp_c);
        OLED_Printf(0, 38, OLED_6X8, "Humi :%u %%", (unsigned int)s_sensor_cache.hum_pct);
        OLED_Printf(0, 50, OLED_6X8, "H Thr:%ld %%", (long)s_thresholds.hum_pct);
    }
    else
    {
        OLED_Printf(0, 18, OLED_6X8, "Temp : --");
        OLED_Printf(0, 34, OLED_6X8, "Humi : --");
        OLED_Printf(0, 50, OLED_6X8, "Thr T:%ld H:%ld", (long)s_thresholds.temp_c, (long)s_thresholds.hum_pct);
    }
}

static void Host_UpdateAlarmState(void)
{
    uint8_t new_mask = Host_CalcAlarmMask();

    s_alarm_state.active_mask = new_mask;

    if ((new_mask != 0U) && (s_alarm_state.previous_mask == 0U))
    {
        s_alarm_state.auto_jump_pending = 1U;
    }

    s_alarm_state.previous_mask = new_mask;

    if (new_mask != 0U)
    {
        Buzzer_On();
    }
    else
    {
        Buzzer_Off();
    }
}

static void Host_UpdateCloudTelemetry(void)
{
    OneNetTelemetry_t telemetry;
    uint8_t slave_online = 0U;
    uint8_t valid_bits = 0U;

    memset(&telemetry, 0, sizeof(telemetry));

    slave_online = ((s_sensor_cache.has_report != 0U) && (s_sensor_cache.is_stale == 0U)) ? 1U : 0U;
    valid_bits = (slave_online != 0U) ? s_sensor_cache.valid_bits : 0U;

    telemetry.temperature = ((valid_bits & ENV_PROTO_VALID_DHT11) != 0U) ? (int32_t)s_sensor_cache.temp_c : 0;
    telemetry.humidity = ((valid_bits & ENV_PROTO_VALID_DHT11) != 0U) ? (int32_t)s_sensor_cache.hum_pct : 0;
    telemetry.pm25 = ((valid_bits & ENV_PROTO_VALID_PM25) != 0U) ? (int32_t)s_sensor_cache.pm25_ugm3 : 0;
    telemetry.mq135_mv = ((valid_bits & ENV_PROTO_VALID_MQ135) != 0U) ? (int32_t)s_sensor_cache.mq135_mv : 0;
    telemetry.gas_percent = ((valid_bits & ENV_PROTO_VALID_MQ135) != 0U) ? (int32_t)Host_GetMq135Percent(s_sensor_cache.mq135_mv) : 0;
    telemetry.light_lux = ((valid_bits & ENV_PROTO_VALID_BH1750) != 0U) ? (int32_t)s_sensor_cache.light_lux : 0;
    telemetry.valid_bits = (int32_t)valid_bits;
    telemetry.slave_online = slave_online;
    telemetry.alarm_active = (s_alarm_state.active_mask != 0U) ? 1U : 0U;
    telemetry.alarm_mask = (int32_t)s_alarm_state.active_mask;
    telemetry.temperature_threshold = s_thresholds.temp_c;
    telemetry.humidity_threshold = s_thresholds.hum_pct;
    telemetry.pm25_threshold = s_thresholds.pm25;
    telemetry.gas_threshold = s_thresholds.mq135_pct;
    telemetry.light_threshold = s_thresholds.light_lux;

    OneNet_UpdateTelemetryCache(&telemetry);
}

const HostSensorCache_t *Menu_GetSensorCache(void)
{
    return &s_sensor_cache;
}

const HostThresholds_t *Menu_GetThresholds(void)
{
    return &s_thresholds;
}

const HostAlarmState_t *Menu_GetAlarmState(void)
{
    return &s_alarm_state;
}

void Menu_ApplyCloudThresholds(const OneNetThresholdUpdate_t *update)
{
    if (update == NULL)
    {
        return;
    }

    if (update->has_pm25_threshold != 0U)
    {
        s_thresholds.pm25 = Host_ClampThreshold(update->pm25_threshold, 0, 999);
    }

    if (update->has_gas_threshold != 0U)
    {
        s_thresholds.mq135_pct = Host_ClampThreshold(update->gas_threshold, 0, 100);
    }

    if (update->has_light_threshold != 0U)
    {
        s_thresholds.light_lux = Host_ClampThreshold(update->light_threshold, 0, 9999);
    }

    if (update->has_temperature_threshold != 0U)
    {
        s_thresholds.temp_c = Host_ClampThreshold(update->temperature_threshold, 0, 50);
    }

    if (update->has_humidity_threshold != 0U)
    {
        s_thresholds.hum_pct = Host_ClampThreshold(update->humidity_threshold, 0, 100);
    }
}

void Menu_Init(void)
{
    memset(&s_sensor_cache, 0, sizeof(s_sensor_cache));
    memset(&s_alarm_state, 0, sizeof(s_alarm_state));

    s_thresholds.pm25 = 75;
    s_thresholds.mq135_pct = 60;
    s_thresholds.light_lux = 1000;
    s_thresholds.temp_c = 30;
    s_thresholds.hum_pct = 80;

    MF_Reset();
    s_root_menu = MF_CreateMenu("Main Menu");
    s_sensor_menu = MF_CreateMenu("Sensors");
    s_threshold_menu = MF_CreateMenu("Thresholds");

    MF_AddSubmenu(s_root_menu, "View Data", s_sensor_menu);
    MF_AddSubmenu(s_root_menu, "Set Thres", s_threshold_menu);
    MF_AddCustomPage(s_root_menu, "Alarm Info", Host_AlarmPage);

    MF_AddCustomPage(s_sensor_menu, "PM2.5", Host_Pm25Page);
    MF_AddCustomPage(s_sensor_menu, "MQ135", Host_Mq135Page);
    MF_AddCustomPage(s_sensor_menu, "Light", Host_LightPage);
    MF_AddCustomPage(s_sensor_menu, "Temp/Humi", Host_TempHumiPage);

    MF_AddValue(s_threshold_menu, "PM2.5", &s_thresholds.pm25, 0, 999, 5, "", NULL);
    MF_AddValue(s_threshold_menu, "MQ135 %", &s_thresholds.mq135_pct, 0, 100, 1, "%", NULL);
    MF_AddValue(s_threshold_menu, "Light", &s_thresholds.light_lux, 0, 9999, 50, "lx", NULL);
    MF_AddValue(s_threshold_menu, "Temp", &s_thresholds.temp_c, 0, 50, 1, "C", NULL);
    MF_AddValue(s_threshold_menu, "Humi", &s_thresholds.hum_pct, 0, 100, 1, "%", NULL);

    MF_Start(s_root_menu);
    Host_SendQueryNow();
    Buzzer_Off();
}

void Menu_Run(void)
{
    KeyEvent_t key = KEY_NONE;

    Zigbee_Task();
    Host_ProcessFrames();
    Host_UpdateLinkState();
    Host_UpdateAlarmState();
    Host_UpdateCloudTelemetry();

    if (s_alarm_state.auto_jump_pending != 0U)
    {
        MF_EnterCustomPage(Host_AlarmPage);
        s_alarm_state.auto_jump_pending = 0U;
    }

    key = Key_Scan();
    MF_Process(key);
    MF_Render();
}
