#include ".\APPLICATION\GatewayApp\GatewayApp.h"

#include ".\APPLICATION\EnvProto\EnvProto.h"
#include ".\APPLICATION\Menu\MenuFramework.h"
#include ".\Hardware\AirM2M_4G\AirM2M_4G.h"
#include ".\Hardware\Buzzer\Buzzer.h"
#include ".\Hardware\LightSensor\LightSensor.h"
#include ".\Hardware\OLED\OLED.h"
#include ".\Hardware\Zigbee\Zigbee.h"
#include "..\..\..\Middlewares\OneNet_MQTT\onenet.h"
#include "..\..\..\Middlewares\OneNet_MQTT\onenet_config.h"
#include "stm32f1xx_hal.h"

#include <stdio.h>

#define GATEWAY_DEFAULT_THRESHOLD_X10      600L
#define GATEWAY_DEFAULT_REPORT_PERIOD_S    30U
#define GATEWAY_LIGHT_READ_PERIOD_MS       2000U
#define GATEWAY_SLAVE_TIMEOUT_MS           10000U
#define GATEWAY_UI_ANIM_REFRESH_MS         16U
#define GATEWAY_UI_LIVE_PAGE_REFRESH_MS    100U
#define GATEWAY_CLOUD_RETRY_MS             15000U
#define GATEWAY_REPORT_RETRY_MS            2000U

typedef struct
{
    float temperature;
    float illuminance;
    int32_t threshold_x10;
    uint16_t report_period_s;
    uint8_t report_period_index;
    uint8_t temperature_valid;
    uint8_t slave_online;
    uint8_t light_online;
    uint8_t alarm_status;
    uint8_t cloud_init_ok;
    uint8_t cloud_ready_latched;
    uint8_t cloud_report_pending;
    uint32_t last_slave_tick;
    uint32_t last_light_tick;
    uint32_t last_oled_tick;
    uint32_t last_cloud_init_tick;
    uint32_t last_report_tick;
    uint32_t last_report_attempt_tick;
} GatewayState_t;

typedef struct
{
    float temperature;
    float illuminance;
    int32_t threshold_x10;
    uint16_t report_period_s;
    uint32_t uptime_s;
    uint8_t temperature_valid;
    uint8_t slave_online;
    uint8_t light_online;
    uint8_t alarm_status;
} GatewayCloudSnapshot_t;

static const uint16_t s_report_periods[] = {10U, 30U, 60U, 300U, 600U};
static const char *s_report_period_labels[] = {"10s", "30s", "1min", "5min", "10min"};

static GatewayState_t s_gateway;
static GatewayCloudSnapshot_t s_cloud_snapshot;
static int32_t s_threshold_c = 60;

static uint8_t GatewayApp_SetReportPeriod(uint16_t seconds);
static void GatewayApp_RequestCloudReport(void);
static void GatewayApp_ProcessZigbeeFrames(void);
static uint8_t GatewayApp_UpdateAlarm(void);
static void GatewayApp_ServiceLocal(void);
static void GatewayApp_ServiceCloud(void);
static const char *GatewayApp_GetReportPeriodLabel(uint8_t index);
static uint8_t GatewayApp_IsNetworkReadyStatus(AirM2M_4G_Status_t status);
static void GatewayApp_FormatValueX10(char *buffer, uint16_t buffer_size,
                                      int32_t value_x10, uint8_t valid, uint8_t allow_negative);
static void GatewayApp_OverviewPage(KeyEvent_t key, uint8_t *exit_flag);
static void GatewayApp_ReportPage(KeyEvent_t key, uint8_t *exit_flag);
static void GatewayApp_DebugPage(KeyEvent_t key, uint8_t *exit_flag);
static void GatewayApp_BuildMenu(void);
static void GatewayApp_ProcessUi(void);
static uint8_t GatewayApp_PublishProperties(const GatewayCloudSnapshot_t *snapshot);
static uint8_t GatewayApp_OnCloudConfig(uint8_t has_threshold, float threshold,
                                        uint8_t has_report_period, int32_t report_period);

static uint8_t GatewayApp_SetReportPeriod(uint16_t seconds)
{
    uint8_t i;

    for (i = 0U; i < (uint8_t)(sizeof(s_report_periods) / sizeof(s_report_periods[0])); i++)
    {
        if (s_report_periods[i] == seconds)
        {
            s_gateway.report_period_s = seconds;
            s_gateway.report_period_index = i;
            return 1U;
        }
    }

    return 0U;
}

static const char *GatewayApp_GetReportPeriodLabel(uint8_t index)
{
    if (index >= (uint8_t)(sizeof(s_report_period_labels) / sizeof(s_report_period_labels[0])))
    {
        return "--";
    }

    return s_report_period_labels[index];
}

static uint8_t GatewayApp_IsNetworkReadyStatus(AirM2M_4G_Status_t status)
{
    switch (status)
    {
    case AIRM2M_4G_STATUS_NET_READY:
    case AIRM2M_4G_STATUS_TCP_CONNECTED:
    case AIRM2M_4G_STATUS_MQTT_CONNECTED:
    case AIRM2M_4G_STATUS_SUBSCRIBED:
        return 1U;
    default:
        return 0U;
    }
}

static void GatewayApp_FormatValueX10(char *buffer, uint16_t buffer_size,
                                      int32_t value_x10, uint8_t valid, uint8_t allow_negative)
{
    uint32_t abs_value;

    if ((buffer == NULL) || (buffer_size == 0U))
    {
        return;
    }

    if (valid == 0U)
    {
        (void)snprintf(buffer, buffer_size, "--.-");
        return;
    }

    if ((allow_negative == 0U) && (value_x10 < 0))
    {
        value_x10 = 0;
    }

    abs_value = (uint32_t)((value_x10 < 0) ? (-value_x10) : value_x10);
    if (value_x10 < 0)
    {
        (void)snprintf(buffer, buffer_size, "-%lu.%lu",
                       (unsigned long)(abs_value / 10U),
                       (unsigned long)(abs_value % 10U));
    }
    else
    {
        (void)snprintf(buffer, buffer_size, "%lu.%lu",
                       (unsigned long)(abs_value / 10U),
                       (unsigned long)(abs_value % 10U));
    }
}

static void GatewayApp_RequestCloudReport(void)
{
    s_cloud_snapshot.temperature = s_gateway.temperature;
    s_cloud_snapshot.illuminance = s_gateway.illuminance;
    s_cloud_snapshot.threshold_x10 = s_gateway.threshold_x10;
    s_cloud_snapshot.report_period_s = s_gateway.report_period_s;
    s_cloud_snapshot.uptime_s = HAL_GetTick() / 1000U;
    s_cloud_snapshot.temperature_valid = s_gateway.temperature_valid;
    s_cloud_snapshot.slave_online = s_gateway.slave_online;
    s_cloud_snapshot.light_online = s_gateway.light_online;
    s_cloud_snapshot.alarm_status = s_gateway.alarm_status;
    s_gateway.cloud_report_pending = 1U;
}

static void GatewayApp_OnThresholdChange(int32_t new_value)
{
    if (new_value < 0)
    {
        new_value = 0;
    }
    if (new_value > 125)
    {
        new_value = 125;
    }
    s_threshold_c = new_value;
    s_gateway.threshold_x10 = new_value * 10L;
    GatewayApp_RequestCloudReport();
}

static void GatewayApp_OverviewPage(KeyEvent_t key, uint8_t *exit_flag)
{
    char temp_text[12];
    char lux_text[12];
    uint32_t uptime;
    int32_t temp_x10;
    int32_t lux_x10;

    if (exit_flag == NULL)
    {
        return;
    }

    if (key == KEY_BACK)
    {
        *exit_flag = 1U;
        return;
    }

    if (key != KEY_NONE)
    {
        return;
    }

    uptime = HAL_GetTick() / 1000U;
    temp_x10 = (int32_t)(s_gateway.temperature * 10.0f);
    lux_x10 = (int32_t)(s_gateway.illuminance * 10.0f);

    GatewayApp_FormatValueX10(temp_text, (uint16_t)sizeof(temp_text), temp_x10,
                              (uint8_t)((s_gateway.slave_online != 0U) && (s_gateway.temperature_valid != 0U)),
                              1U);
    GatewayApp_FormatValueX10(lux_text, (uint16_t)sizeof(lux_text), lux_x10,
                              s_gateway.light_online, 0U);

    OLED_Printf(0, 0, OLED_8X16, "T:%sC TH:%ld", temp_text, (long)s_threshold_c);
    OLED_Printf(0, 16, OLED_8X16, "L:%sLux", lux_text);
    OLED_Printf(0, 32, OLED_8X16, "ALM:%s SLV:%s",
                s_gateway.alarm_status ? "ON" : "OFF",
                s_gateway.slave_online ? "ON" : "OFF");
    OLED_Printf(0, 48, OLED_6X8, "UP:%lus RP:%us",
                (unsigned long)uptime,
                (unsigned int)s_gateway.report_period_s);
}

static void GatewayApp_ReportPage(KeyEvent_t key, uint8_t *exit_flag)
{
    const char *period_label;

    if (exit_flag == NULL)
    {
        return;
    }

    if (key == KEY_UP)
    {
        if (s_gateway.report_period_index == 0U)
        {
            s_gateway.report_period_index = (uint8_t)(sizeof(s_report_periods) / sizeof(s_report_periods[0]) - 1U);
        }
        else
        {
            s_gateway.report_period_index--;
        }
        s_gateway.report_period_s = s_report_periods[s_gateway.report_period_index];
        GatewayApp_RequestCloudReport();
    }
    else if (key == KEY_DOWN)
    {
        s_gateway.report_period_index++;
        if (s_gateway.report_period_index >= (uint8_t)(sizeof(s_report_periods) / sizeof(s_report_periods[0])))
        {
            s_gateway.report_period_index = 0U;
        }
        s_gateway.report_period_s = s_report_periods[s_gateway.report_period_index];
        GatewayApp_RequestCloudReport();
    }
    else if ((key == KEY_ENTER) || (key == KEY_BACK))
    {
        *exit_flag = 1U;
        return;
    }

    if (key == KEY_NONE)
    {
        period_label = GatewayApp_GetReportPeriodLabel(s_gateway.report_period_index);
        OLED_ShowString(36, 0, "Report", OLED_8X16);
        OLED_ShowString(4, 20, "UP/DN Select", OLED_8X16);
        OLED_Printf(28, 40, OLED_8X16, "%s", (char *)period_label);
        OLED_ShowString(34, 56, "OK/BACK", OLED_6X8);
    }
}

static void GatewayApp_DebugPage(KeyEvent_t key, uint8_t *exit_flag)
{
    const char *last_line;
    const char *post_reply_msg;
    AirM2M_4G_Status_t status;
    uint16_t post_reply_code;

    if (exit_flag == NULL)
    {
        return;
    }

    if ((key == KEY_ENTER) || (key == KEY_BACK))
    {
        *exit_flag = 1U;
        return;
    }

    if (key == KEY_NONE)
    {
        status = AirM2M_4G_GetStatus();
        last_line = AirM2M_4G_GetLastLine();
        post_reply_code = OneNet_GetLastPostReplyCode();
        post_reply_msg = OneNet_GetLastPostReplyMsg();
        if ((last_line == NULL) || (last_line[0] == '\0'))
        {
            last_line = "N/A";
        }
        if ((post_reply_msg == NULL) || (post_reply_msg[0] == '\0'))
        {
            post_reply_msg = "--";
        }

        OLED_ShowString(28, 0, "4G Debug", OLED_8X16);
        OLED_Printf(0, 18, OLED_8X16, "4G:%s", AirM2M_4G_GetStatusText());
        OLED_Printf(0, 38, OLED_6X8, "NET:%u MQ:%u CLD:%u SB:%u",
                    (unsigned int)GatewayApp_IsNetworkReadyStatus(status),
                    (unsigned int)OneNet_IsConnected(),
                    (unsigned int)OneNet_IsReady(),
                    (unsigned int)AirM2M_4G_IsSubscribed());
        if (post_reply_code != 0U)
        {
            OLED_Printf(0, 54, OLED_6X8, "PR:%u %.10s",
                        (unsigned int)post_reply_code,
                        (char *)post_reply_msg);
        }
        else
        {
            OLED_Printf(0, 54, OLED_6X8, "RX:%.18s", (char *)last_line);
        }
    }
}

static void GatewayApp_BuildMenu(void)
{
    MF_Menu_t *root;

    MF_Reset();
    root = MF_CreateMenu("Main");
    if (root == NULL)
    {
        return;
    }

    MF_AddCustomPage(root, "Overview", GatewayApp_OverviewPage);
    MF_AddValue(root, "Temp TH", &s_threshold_c, 0, 125, 1, "C", GatewayApp_OnThresholdChange);
    MF_AddCustomPage(root, "Report", GatewayApp_ReportPage);
    MF_AddCustomPage(root, "4G Debug", GatewayApp_DebugPage);

    MF_Start(root);
    MF_Process(KEY_ENTER);
}

static void GatewayApp_ProcessZigbeeFrames(void)
{
    EnvProto_Frame_t frame;
    EnvProto_TempReport_t report;
    uint8_t prev_online;
    uint8_t prev_valid;

    Zigbee_Task();
    while (Zigbee_GetFrame(&frame) != 0U)
    {
        if ((EnvProto_ParseTempReport(&frame, &report) != 0U) && (report.node_id == 1U))
        {
            prev_online = s_gateway.slave_online;
            prev_valid = s_gateway.temperature_valid;

            if (report.status == ENVPROTO_TEMP_STATUS_OK)
            {
                s_gateway.temperature = (float)report.temperature_x10 / 10.0f;
                s_gateway.temperature_valid = 1U;
            }
            else
            {
                s_gateway.temperature_valid = 0U;
            }

            s_gateway.last_slave_tick = HAL_GetTick();
            s_gateway.slave_online = 1U;

            if ((prev_online != s_gateway.slave_online) || (prev_valid != s_gateway.temperature_valid))
            {
                GatewayApp_RequestCloudReport();
            }
        }
    }
}

static void GatewayApp_WaitCallback(void)
{
    GatewayApp_ServiceLocal();
    GatewayApp_ProcessUi();
}

static uint8_t GatewayApp_UpdateAlarm(void)
{
    int32_t temp_x10 = (int32_t)(s_gateway.temperature * 10.0f);
    uint8_t prev_alarm = s_gateway.alarm_status;

    if ((s_gateway.slave_online != 0U) && (s_gateway.temperature_valid != 0U) &&
        (temp_x10 >= s_gateway.threshold_x10))
    {
        s_gateway.alarm_status = 1U;
        Buzzer_On();
    }
    else
    {
        s_gateway.alarm_status = 0U;
        Buzzer_Off();
    }

    return (prev_alarm != s_gateway.alarm_status) ? 1U : 0U;
}

static void GatewayApp_ServiceLocal(void)
{
    uint32_t now = HAL_GetTick();
    float lux;
    uint8_t sync_needed = 0U;
    uint8_t prev_light_online;

    GatewayApp_ProcessZigbeeFrames();

    if ((s_gateway.slave_online != 0U) &&
        ((now - s_gateway.last_slave_tick) > GATEWAY_SLAVE_TIMEOUT_MS))
    {
        s_gateway.slave_online = 0U;
        s_gateway.temperature_valid = 0U;
        sync_needed = 1U;
    }

    if ((now - s_gateway.last_light_tick) >= GATEWAY_LIGHT_READ_PERIOD_MS)
    {
        prev_light_online = s_gateway.light_online;
        s_gateway.last_light_tick = now;
        if (LightSensor_ReadLux(&lux) == LIGHT_SENSOR_OK)
        {
            s_gateway.illuminance = lux;
            s_gateway.light_online = 1U;
        }
        else
        {
            s_gateway.light_online = 0U;
        }
        if (prev_light_online != s_gateway.light_online)
        {
            sync_needed = 1U;
        }
    }

    if (GatewayApp_UpdateAlarm() != 0U)
    {
        sync_needed = 1U;
    }

    if (sync_needed != 0U)
    {
        GatewayApp_RequestCloudReport();
    }
}

static void GatewayApp_ProcessUi(void)
{
    KeyEvent_t key = Key_Scan();
    uint32_t now = HAL_GetTick();
    uint32_t refresh_interval = 0U;
    uint8_t need_render = 0U;

    MF_Process(key);

    if (key != KEY_NONE)
    {
        need_render = 1U;
    }
    else if (MF_IsAnimating() != 0U)
    {
        refresh_interval = GATEWAY_UI_ANIM_REFRESH_MS;
    }
    else if (MF_IsCustomPageActive() != 0U)
    {
        refresh_interval = GATEWAY_UI_LIVE_PAGE_REFRESH_MS;
    }

    if ((need_render == 0U) && (refresh_interval != 0U) &&
        ((now - s_gateway.last_oled_tick) >= refresh_interval))
    {
        need_render = 1U;
    }

    if (need_render != 0U)
    {
        s_gateway.last_oled_tick = now;
        MF_Render();
    }
}

static uint8_t GatewayApp_OnCloudConfig(uint8_t has_threshold, float threshold,
                                        uint8_t has_report_period, int32_t report_period)
{
    int32_t threshold_x10;

    if (has_threshold != 0U)
    {
        if ((threshold < 0.0f) || (threshold > 125.0f))
        {
            return 0U;
        }
    }

    if (has_report_period != 0U)
    {
        if (GatewayApp_SetReportPeriod((uint16_t)report_period) == 0U)
        {
            return 0U;
        }
    }

    if (has_threshold != 0U)
    {
        threshold_x10 = (int32_t)(threshold * 10.0f + 0.5f);
        s_gateway.threshold_x10 = threshold_x10;
        s_threshold_c = (threshold_x10 + 5L) / 10L;
    }

    GatewayApp_RequestCloudReport();
    return 1U;
}

static void GatewayApp_TryCloudInit(void)
{
    uint32_t now = HAL_GetTick();

    if (s_gateway.cloud_init_ok != 0U)
    {
        OneNet_Process();
        return;
    }

    if ((now - s_gateway.last_cloud_init_tick) < GATEWAY_CLOUD_RETRY_MS)
    {
        return;
    }

    s_gateway.last_cloud_init_tick = now;
    OneNet_RegisterPropertySetCallback(GatewayApp_OnCloudConfig);
    AirM2M_4G_RegisterWaitCallback(GatewayApp_WaitCallback);
    s_gateway.cloud_init_ok = OneNet_Init();
}

static uint8_t GatewayApp_PublishProperties(const GatewayCloudSnapshot_t *snapshot)
{
    OneNetProperty_t props[ONENET_PROPERTY_COUNT];
    uint8_t i = 0U;

    if ((snapshot == NULL) || (OneNet_IsReady() == 0U))
    {
        return 0U;
    }

    props[i].name = ONENET_PROPERTY_ALARM_STATUS;
    props[i].type = ONENET_VALUE_BOOL;
    props[i].value.boolean = snapshot->alarm_status;
    props[i].precision = 0U;
    i++;

    props[i].name = ONENET_PROPERTY_ILLUMINANCE;
    props[i].type = ONENET_VALUE_FLOAT;
    props[i].value.f32 = snapshot->illuminance;
    props[i].precision = 2U;
    i++;

    props[i].name = ONENET_PROPERTY_LIGHT_ONLINE;
    props[i].type = ONENET_VALUE_BOOL;
    props[i].value.boolean = snapshot->light_online;
    props[i].precision = 0U;
    i++;

    props[i].name = ONENET_PROPERTY_REPORT_PERIOD;
    props[i].type = ONENET_VALUE_INT;
    props[i].value.i32 = (int32_t)snapshot->report_period_s;
    props[i].precision = 0U;
    i++;

    props[i].name = ONENET_PROPERTY_SLAVE_ONLINE;
    props[i].type = ONENET_VALUE_BOOL;
    props[i].value.boolean = snapshot->slave_online;
    props[i].precision = 0U;
    i++;

    props[i].name = ONENET_PROPERTY_TEMPERATURE;
    props[i].type = ONENET_VALUE_FLOAT;
    props[i].value.f32 = snapshot->temperature;
    props[i].precision = 1U;
    i++;

    props[i].name = ONENET_PROPERTY_TEMPERATURE_THRESHOLD;
    props[i].type = ONENET_VALUE_FLOAT;
    props[i].value.f32 = (float)snapshot->threshold_x10 / 10.0f;
    props[i].precision = 1U;
    i++;

    props[i].name = ONENET_PROPERTY_UPTIME;
    props[i].type = ONENET_VALUE_INT;
    props[i].value.i32 = (int32_t)snapshot->uptime_s;
    props[i].precision = 0U;
    i++;

    return OneNet_PublishProperties(props, i);
}

static void GatewayApp_ServiceCloud(void)
{
    uint32_t now = HAL_GetTick();
    uint8_t ready;

    GatewayApp_TryCloudInit();
    ready = OneNet_IsReady();
    if ((ready != 0U) && (s_gateway.cloud_ready_latched == 0U))
    {
        GatewayApp_RequestCloudReport();
    }
    s_gateway.cloud_ready_latched = ready;

    if ((now - s_gateway.last_report_tick) >= ((uint32_t)s_gateway.report_period_s * 1000U))
    {
        s_gateway.last_report_tick = now;
        GatewayApp_RequestCloudReport();
    }

    if ((s_gateway.cloud_report_pending == 0U) || (ready == 0U))
    {
        return;
    }

    if ((s_gateway.last_report_attempt_tick != 0U) &&
        ((now - s_gateway.last_report_attempt_tick) < GATEWAY_REPORT_RETRY_MS))
    {
        return;
    }

    s_gateway.last_report_attempt_tick = now;
    if (GatewayApp_PublishProperties(&s_cloud_snapshot) != 0U)
    {
        s_gateway.cloud_report_pending = 0U;
    }
}

void GatewayApp_Init(void)
{
    s_gateway.temperature = 0.0f;
    s_gateway.illuminance = 0.0f;
    s_gateway.threshold_x10 = GATEWAY_DEFAULT_THRESHOLD_X10;
    s_gateway.temperature_valid = 0U;
    s_threshold_c = s_gateway.threshold_x10 / 10L;
    (void)GatewayApp_SetReportPeriod(GATEWAY_DEFAULT_REPORT_PERIOD_S);
    s_gateway.cloud_ready_latched = 0U;
    s_gateway.cloud_report_pending = 0U;
    s_gateway.last_cloud_init_tick = HAL_GetTick() - GATEWAY_CLOUD_RETRY_MS;
    s_gateway.last_report_tick = HAL_GetTick();
    s_gateway.last_report_attempt_tick = 0U;

    Buzzer_Init();
    Zigbee_Init();
    LightSensor_Init();
    GatewayApp_BuildMenu();

    OneNet_RegisterPropertySetCallback(GatewayApp_OnCloudConfig);
    AirM2M_4G_RegisterWaitCallback(GatewayApp_WaitCallback);
    s_gateway.cloud_init_ok = OneNet_Init();
    s_gateway.last_oled_tick = HAL_GetTick();
    MF_Render();
}

void GatewayApp_Task(void)
{
    GatewayApp_ServiceLocal();
    GatewayApp_ServiceCloud();
    GatewayApp_ProcessUi();
}
