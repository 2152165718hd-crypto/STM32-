#include "Menu.h"
#include "MenuFramework.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

static ADC_HandleTypeDef hadc1;

#define AZDM01_PA6_DIVIDER_UPPER        4.7f
#define AZDM01_PA6_DIVIDER_LOWER        6.7f
#define AZDM01_PA6_TO_OUTPUT_SCALE      (AZDM01_PA6_DIVIDER_LOWER / AZDM01_PA6_DIVIDER_UPPER)

#define SENSOR_ENV_PERIOD_MS            1000u
#define SENSOR_DHT_PERIOD_MS            2000u
#define SCREEN_REFRESH_PERIOD_MS        250u
#define BUZZER_TOGGLE_PERIOD_MS         250u
#define PM_STALE_TIMEOUT_MS             5000u
#define PM_WARMUP_MS                    10000u
#define APP_LOOP_DELAY_MS               1u

#define WATER_SAMPLE_COUNT              8u
#define NOISE_SAMPLE_COUNT              16u

#define NOISE_DB_MIN                    30u
#define NOISE_DB_MAX                    120u

#define TH_TEMP_MIN                     0
#define TH_TEMP_MAX                     60
#define TH_TEMP_STEP                    1

#define TH_HUMI_MIN                     0
#define TH_HUMI_MAX                     100
#define TH_HUMI_STEP                    1

#define TH_PM25_MIN                     0
#define TH_PM25_MAX                     500
#define TH_PM25_STEP                    5

#define TH_WATER_MIN                    0
#define TH_WATER_MAX                    4000
#define TH_WATER_STEP                   50

#define TH_NOISE_MIN                    30
#define TH_NOISE_MAX                    120
#define TH_NOISE_STEP                   1

#define COMMAND_LOG_DEPTH               5u
#define COMMAND_LOG_TEXT_MAX            31u

typedef enum
{
    TXT_TITLE_MAIN = 0,
    TXT_MENU_VIEW,
    TXT_MENU_THRESHOLD,
    TXT_MENU_ESP,
    TXT_MENU_ALARM,
    TXT_TITLE_ALARM,
    TXT_TITLE_SENSOR,
    TXT_ITEM_TEMP_HUMI,
    TXT_ITEM_PM25,
    TXT_ITEM_WATER,
    TXT_ITEM_NOISE,
    TXT_TITLE_THRESHOLD,
    TXT_ITEM_TEMP_TH,
    TXT_ITEM_HUMI_TH,
    TXT_ITEM_PM25_TH,
    TXT_ITEM_WATER_TH,
    TXT_ITEM_NOISE_TH,
    TXT_TITLE_ESP,
    TXT_ITEM_ESP_STATE,
    TXT_ITEM_CMD_LOG,
    TXT_HINT_BACK,
    TXT_HINT_EMPTY,
    TXT_ALERT_AIR,
    TXT_STATE_READY,
    TXT_STATE_FAIL,
    TXT_STATE_LINKED,
    TXT_STATE_IDLE,
    TXT_STATE_ON,
    TXT_STATE_OFF,
    TXT_STATUS_SAFE,
    TXT_STATUS_ALERT
} MenuTextId_t;

typedef struct
{
    int16_t temp_c;
    uint16_t humi_pct;
    uint16_t pm25;
    uint16_t water_ntu;
    uint16_t noise_db;
    uint8_t temp_humi_valid;
    uint8_t pm_valid;
    uint8_t water_valid;
    uint8_t noise_valid;
} SensorSnapshot_t;

typedef struct
{
    int32_t temp_c;
    int32_t humi_pct;
    int32_t pm25;
    int32_t water_ntu;
    int32_t noise_db;
} ThresholdConfig_t;

typedef struct
{
    uint8_t temp;
    uint8_t humi;
    uint8_t pm25;
    uint8_t water;
    uint8_t noise;
} AlarmState_t;

typedef struct
{
    uint8_t any_alarm;
    uint8_t buzzer_on;
    uint8_t esp_ready;
    uint8_t has_remote_link;
    uint8_t remote_link_id;
    uint8_t remote_query_pending;
    uint8_t alarm_page_auto_pending;
    uint8_t alarm_page_auto_muted;
    uint32_t rx_cmd_count;
    uint32_t tx_frame_count;
    uint32_t invalid_cmd_count;
    uint32_t alarm_push_count;
} SystemFlags_t;

typedef struct
{
    char entries[COMMAND_LOG_DEPTH][COMMAND_LOG_TEXT_MAX + 1u];
    uint8_t count;
} CommandLog_t;

static AZDM01_HandleTypeDef g_azdm01;
static AZDM01_ResultTypeDef g_azdm01_result;

static SensorSnapshot_t g_sensor = {0};
static ThresholdConfig_t g_threshold = {35, 80, 90, 1000, 70};
static const ThresholdConfig_t g_hysteresis = {1, 3, 5, 50, 3};
static AlarmState_t g_alarm = {0};
static SystemFlags_t g_flags = {0};
static CommandLog_t g_command_log = {{{0}}, 0u};

static uint8_t g_screen_dirty = 0u;
static uint32_t g_last_env_tick = 0u;
static uint32_t g_last_dht_tick = 0u;
static uint32_t g_last_screen_tick = 0u;
static uint32_t g_last_buzzer_tick = 0u;
static uint32_t g_last_pm_success_tick = 0u;
static uint32_t g_pm_start_tick = 0u;

static MF_Menu_t *g_root_menu = NULL;
static MF_Menu_t *g_sensor_menu = NULL;
static MF_Menu_t *g_threshold_menu = NULL;
static MF_Menu_t *g_esp_menu = NULL;

static const char *const g_menu_text[] = {
    "Main Menu",
    "View Data",
    "Set Limits",
    "ESP Debug",
    "Alarm Page",
    "ALARM PAGE",
    "Sensor Data",
    "Temp/Humi",
    "PM2.5",
    "Water NTU",
    "Noise dB",
    "Alarm Limits",
    "Temp",
    "Humidity",
    "PM2.5",
    "Water NTU",
    "Noise dB",
    "ESP Debug",
    "ESP Status",
    "Command Log",
    "BACK TO EXIT",
    "No Commands",
    "AIR ALERT",
    "READY",
    "FAIL",
    "LINKED",
    "IDLE",
    "ON",
    "OFF",
    "SAFE",
    "ALERT"
};

static const char *Menu_Text(MenuTextId_t id);
static void Menu_SystemErrorHandler(void);
static void Menu_MarkScreenDirty(void);
static void Menu_BuildTree(void);
static void Menu_DoInitialSample(void);
static void Menu_ProcessKeyEvents(void);
static void Menu_ServicePeriodicTasks(void);
static void Menu_ServiceRemoteQuery(void);
static void Menu_ServiceBuzzer(void);
static void Menu_ServiceAlarmPageAutoEntry(void);
static void Menu_EvaluateAlarms(void);
static void Menu_RecordCommand(const char *text);
static void Menu_FormatDataField(char *buf, uint16_t buf_size, uint8_t valid, int32_t value);
static void Menu_DrawTitle(const char *title);
static void Menu_DrawDetailPage(const char *title,
                                const char *line1,
                                const char *line2,
                                const char *line3,
                                const char *line4,
                                const char *footer);
static void Menu_RenderTempHumiPage(KeyEvent_t key, uint8_t *exit_flag);
static void Menu_RenderPm25Page(KeyEvent_t key, uint8_t *exit_flag);
static void Menu_RenderWaterPage(KeyEvent_t key, uint8_t *exit_flag);
static void Menu_RenderNoisePage(KeyEvent_t key, uint8_t *exit_flag);
static void Menu_RenderAlarmPage(KeyEvent_t key, uint8_t *exit_flag);
static void Menu_RenderEspStatePage(KeyEvent_t key, uint8_t *exit_flag);
static void Menu_RenderCommandLogPage(KeyEvent_t key, uint8_t *exit_flag);
static void Menu_OnThresholdChanged(int32_t new_value);
static HAL_StatusTypeDef Menu_ADC1SelectChannel(uint32_t channel, uint32_t sample_time);
static HAL_StatusTypeDef Menu_ADC1ReadAverage(uint8_t samples, uint16_t *raw);
static void Menu_MX_ADC1_Init(void);
static uint16_t Menu_NoisedBFromRaw(uint16_t raw);
static uint8_t Menu_UpdateAlarmWithHysteresis(uint8_t active, int32_t value, int32_t threshold, int32_t hysteresis);
static uint8_t Menu_IsTelemetryCommand(const char *cmd_upper);
static void Menu_TrimString(char *str);
static void Menu_ToUpperString(char *str);
static void Menu_ParseRemoteCommand(ESP_DataPacket_t *packet);
static void Menu_ESPUserPacketCallback(ESP_DataPacket_t *packet);
static void Menu_SendTelemetry(uint8_t link_id);
static void Menu_SendProtocolError(uint8_t link_id);
static void Menu_SendAlarm(const char *sensor, uint8_t active, int32_t value, int32_t threshold);
static void Menu_SendAlarmIfChanged(const char *sensor,
                                    uint8_t prev_active,
                                    uint8_t current_active,
                                    int32_t value,
                                    int32_t threshold);
static void Menu_SampleTempHumi(void);
static void Menu_SamplePm25(void);
static void Menu_SampleWaterNtu(void);
static void Menu_SampleNoiseDb(void);

static const char *Menu_Text(MenuTextId_t id)
{
    if ((uint32_t)id >= (sizeof(g_menu_text) / sizeof(g_menu_text[0])))
    {
        return "";
    }
    return g_menu_text[id];
}

static void Menu_SystemErrorHandler(void)
{
    while (1)
    {
    }
}

static void Menu_MarkScreenDirty(void)
{
    g_screen_dirty = 1u;
}

void HAL_ADC_MspInit(ADC_HandleTypeDef *hadc)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    if ((hadc == NULL) || (hadc->Instance != ADC1))
    {
        return;
    }

    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_ADC_CONFIG(RCC_ADCPCLK2_DIV6);

    GPIO_InitStruct.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

static void Menu_MX_ADC1_Init(void)
{
    ADC_ChannelConfTypeDef sConfig = {0};

    hadc1.Instance = ADC1;
    hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc1.Init.ContinuousConvMode = DISABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion = 1;
    if (HAL_ADC_Init(&hadc1) != HAL_OK)
    {
        Menu_SystemErrorHandler();
    }

    sConfig.Channel = ADC_CHANNEL_6;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_71CYCLES_5;
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
    {
        Menu_SystemErrorHandler();
    }

    if (HAL_ADCEx_Calibration_Start(&hadc1) != HAL_OK)
    {
        Menu_SystemErrorHandler();
    }
}

static HAL_StatusTypeDef Menu_ADC1SelectChannel(uint32_t channel, uint32_t sample_time)
{
    ADC_ChannelConfTypeDef sConfig = {0};

    sConfig.Channel = channel;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = sample_time;
    return HAL_ADC_ConfigChannel(&hadc1, &sConfig);
}

static HAL_StatusTypeDef Menu_ADC1ReadAverage(uint8_t samples, uint16_t *raw)
{
    uint8_t i;
    uint8_t count;
    uint32_t sum = 0u;

    if (raw == NULL)
    {
        return HAL_ERROR;
    }

    count = (samples == 0u) ? 1u : samples;
    for (i = 0u; i < count; i++)
    {
        if (HAL_ADC_Start(&hadc1) != HAL_OK)
        {
            return HAL_ERROR;
        }
        if (HAL_ADC_PollForConversion(&hadc1, 10u) != HAL_OK)
        {
            (void)HAL_ADC_Stop(&hadc1);
            return HAL_ERROR;
        }
        sum += HAL_ADC_GetValue(&hadc1);
        (void)HAL_ADC_Stop(&hadc1);
    }

    *raw = (uint16_t)(sum / count);
    return HAL_OK;
}

static uint16_t Menu_NoisedBFromRaw(uint16_t raw)
{
    uint32_t db;

    if (raw > 4095u)
    {
        raw = 4095u;
    }

    db = NOISE_DB_MIN + ((uint32_t)raw * (NOISE_DB_MAX - NOISE_DB_MIN)) / 4095u;
    if (db > NOISE_DB_MAX)
    {
        db = NOISE_DB_MAX;
    }
    return (uint16_t)db;
}

static uint8_t Menu_UpdateAlarmWithHysteresis(uint8_t active, int32_t value, int32_t threshold, int32_t hysteresis)
{
    int32_t clear_threshold = threshold - hysteresis;

    if (clear_threshold < 0)
    {
        clear_threshold = 0;
    }

    if (active == 0u)
    {
        return (value >= threshold) ? 1u : 0u;
    }

    return (value <= clear_threshold) ? 0u : 1u;
}

static void Menu_FormatDataField(char *buf, uint16_t buf_size, uint8_t valid, int32_t value)
{
    if ((buf == NULL) || (buf_size == 0u))
    {
        return;
    }

    if (valid == 0u)
    {
        (void)snprintf(buf, buf_size, "NA");
    }
    else
    {
        (void)snprintf(buf, buf_size, "%ld", (long)value);
    }
}

static void Menu_RecordCommand(const char *text)
{
    uint8_t i;
    const char *log_text = text;

    if ((log_text == NULL) || (log_text[0] == '\0'))
    {
        log_text = "<EMPTY>";
    }

    for (i = COMMAND_LOG_DEPTH - 1u; i > 0u; i--)
    {
        (void)memcpy(g_command_log.entries[i], g_command_log.entries[i - 1u], COMMAND_LOG_TEXT_MAX + 1u);
    }

    (void)strncpy(g_command_log.entries[0], log_text, COMMAND_LOG_TEXT_MAX);
    g_command_log.entries[0][COMMAND_LOG_TEXT_MAX] = '\0';

    if (g_command_log.count < COMMAND_LOG_DEPTH)
    {
        g_command_log.count++;
    }

    Menu_MarkScreenDirty();
}

static void Menu_TrimString(char *str)
{
    size_t len;
    size_t start = 0u;

    if (str == NULL)
    {
        return;
    }

    len = strlen(str);
    while ((len > 0u) &&
           ((str[len - 1u] == '\r') || (str[len - 1u] == '\n') ||
            (str[len - 1u] == ' ') || (str[len - 1u] == '\t')))
    {
        str[len - 1u] = '\0';
        len--;
    }

    while ((str[start] == ' ') || (str[start] == '\t') ||
           (str[start] == '\r') || (str[start] == '\n'))
    {
        start++;
    }

    if (start > 0u)
    {
        (void)memmove(str, &str[start], strlen(&str[start]) + 1u);
    }
}

static void Menu_ToUpperString(char *str)
{
    size_t i;

    if (str == NULL)
    {
        return;
    }

    for (i = 0u; str[i] != '\0'; i++)
    {
        if ((str[i] >= 'a') && (str[i] <= 'z'))
        {
            str[i] = (char)(str[i] - 'a' + 'A');
        }
    }
}

static uint8_t Menu_IsTelemetryCommand(const char *cmd_upper)
{
    if (cmd_upper == NULL)
    {
        return 0u;
    }

    if (strcmp(cmd_upper, "GET") == 0)
    {
        return 1u;
    }

    if (strcmp(cmd_upper, "GET,ALL") == 0)
    {
        return 1u;
    }

    return 0u;
}

static void Menu_SendProtocolError(uint8_t link_id)
{
    if (g_flags.esp_ready == 0u)
    {
        return;
    }

    if (ESP_SendString(link_id, "ERR,UNKNOWN_CMD\r\n") == ESP_OK)
    {
        g_flags.tx_frame_count++;
    }
}

static void Menu_SendTelemetry(uint8_t link_id)
{
    char temp_buf[12];
    char humi_buf[12];
    char pm25_buf[12];
    char water_buf[12];
    char noise_buf[12];
    char tx[ESP_TX_BUF_SIZE];
    int n;

    if (g_flags.esp_ready == 0u)
    {
        return;
    }

    Menu_FormatDataField(temp_buf, sizeof(temp_buf), g_sensor.temp_humi_valid, g_sensor.temp_c);
    Menu_FormatDataField(humi_buf, sizeof(humi_buf), g_sensor.temp_humi_valid, g_sensor.humi_pct);
    Menu_FormatDataField(pm25_buf, sizeof(pm25_buf), g_sensor.pm_valid, g_sensor.pm25);
    Menu_FormatDataField(water_buf, sizeof(water_buf), g_sensor.water_valid, g_sensor.water_ntu);
    Menu_FormatDataField(noise_buf, sizeof(noise_buf), g_sensor.noise_valid, g_sensor.noise_db);

    n = snprintf(tx, sizeof(tx),
                 "DATA,T=%s,H=%s,PM25=%s,W=%s,N=%s,TH_T=%ld,TH_H=%ld,TH_PM25=%ld,TH_W=%ld,TH_N=%ld\r\n",
                 temp_buf,
                 humi_buf,
                 pm25_buf,
                 water_buf,
                 noise_buf,
                 (long)g_threshold.temp_c,
                 (long)g_threshold.humi_pct,
                 (long)g_threshold.pm25,
                 (long)g_threshold.water_ntu,
                 (long)g_threshold.noise_db);

    if ((n <= 0) || (n >= (int)sizeof(tx)))
    {
        (void)snprintf(tx, sizeof(tx), "ERR,FMT\r\n");
    }

    if (ESP_SendString(link_id, tx) == ESP_OK)
    {
        g_flags.tx_frame_count++;
    }
    else
    {
        g_flags.has_remote_link = 0u;
    }
}

static void Menu_SendAlarm(const char *sensor, uint8_t active, int32_t value, int32_t threshold)
{
    char tx[ESP_TX_BUF_SIZE];
    int n;

    if ((g_flags.esp_ready == 0u) || (g_flags.has_remote_link == 0u))
    {
        return;
    }

    n = snprintf(tx, sizeof(tx),
                 "ALARM,%s,%s,VAL=%ld,TH=%ld\r\n",
                 sensor,
                 (active != 0u) ? "ON" : "OFF",
                 (long)value,
                 (long)threshold);

    if ((n <= 0) || (n >= (int)sizeof(tx)))
    {
        (void)snprintf(tx, sizeof(tx), "ALARM,%s,%s\r\n",
                       sensor,
                       (active != 0u) ? "ON" : "OFF");
    }

    if (ESP_SendString(g_flags.remote_link_id, tx) == ESP_OK)
    {
        g_flags.tx_frame_count++;
        g_flags.alarm_push_count++;
    }
    else
    {
        g_flags.has_remote_link = 0u;
    }
}

static void Menu_SendAlarmIfChanged(const char *sensor,
                                    uint8_t prev_active,
                                    uint8_t current_active,
                                    int32_t value,
                                    int32_t threshold)
{
    if ((prev_active == 0u) && (current_active != 0u))
    {
        Menu_SendAlarm(sensor, 1u, value, threshold);
    }
    else if ((prev_active != 0u) && (current_active == 0u))
    {
        Menu_SendAlarm(sensor, 0u, value, threshold);
    }
}

static void Menu_ParseRemoteCommand(ESP_DataPacket_t *packet)
{
    char raw_cmd[ESP_DATA_BUF_SIZE + 1u];
    char upper_cmd[ESP_DATA_BUF_SIZE + 1u];
    uint16_t copy_len;

    if (packet == NULL)
    {
        return;
    }

    copy_len = packet->len;
    if (copy_len > ESP_DATA_BUF_SIZE)
    {
        copy_len = ESP_DATA_BUF_SIZE;
    }

    (void)memcpy(raw_cmd, packet->data, copy_len);
    raw_cmd[copy_len] = '\0';
    Menu_TrimString(raw_cmd);
    Menu_RecordCommand(raw_cmd);

    (void)strncpy(upper_cmd, raw_cmd, ESP_DATA_BUF_SIZE);
    upper_cmd[ESP_DATA_BUF_SIZE] = '\0';
    Menu_ToUpperString(upper_cmd);

    g_flags.rx_cmd_count++;

    if (Menu_IsTelemetryCommand(upper_cmd) != 0u)
    {
        g_flags.remote_link_id = packet->link_id;
        g_flags.has_remote_link = 1u;
        g_flags.remote_query_pending = 1u;
    }
    else
    {
        g_flags.invalid_cmd_count++;
        Menu_SendProtocolError(packet->link_id);
    }

    Menu_MarkScreenDirty();
}

static void Menu_ESPUserPacketCallback(ESP_DataPacket_t *packet)
{
    Menu_ParseRemoteCommand(packet);
}

static void Menu_SampleTempHumi(void)
{
    if (DHT11_ReadData() == DHT11_STATUS_OK)
    {
        g_sensor.temp_c = (int16_t)DHT11_Data.temp_int;
        g_sensor.humi_pct = (uint16_t)DHT11_Data.humi_int;
        g_sensor.temp_humi_valid = 1u;
        AZDM01_SetTemperature(&g_azdm01, (float)g_sensor.temp_c);
    }
    else
    {
        g_sensor.temp_humi_valid = 0u;
    }
}

static void Menu_SamplePm25(void)
{
    uint16_t pm25;
    uint16_t pm10;
    uint32_t now = HAL_GetTick();

    if ((now - g_pm_start_tick) < PM_WARMUP_MS)
    {
        (void)DC01_ReadPMugm3(&pm25, &pm10);
        g_sensor.pm_valid = 0u;
        return;
    }

    if (DC01_ReadPMugm3(&pm25, &pm10) != 0u)
    {
        (void)pm10;
        g_sensor.pm25 = pm25;
        g_sensor.pm_valid = 1u;
        g_last_pm_success_tick = now;
    }
    else if ((now - g_last_pm_success_tick) > PM_STALE_TIMEOUT_MS)
    {
        g_sensor.pm_valid = 0u;
    }
}

static void Menu_SampleWaterNtu(void)
{
    if (Menu_ADC1SelectChannel(ADC_CHANNEL_6, ADC_SAMPLETIME_71CYCLES_5) != HAL_OK)
    {
        g_sensor.water_valid = 0u;
        return;
    }

    if (AZDM01_Measure(&g_azdm01, WATER_SAMPLE_COUNT, &g_azdm01_result) == HAL_OK)
    {
        g_sensor.water_ntu = g_azdm01_result.ntuEstimate;
        g_sensor.water_valid = 1u;
    }
    else
    {
        g_sensor.water_valid = 0u;
    }
}

static void Menu_SampleNoiseDb(void)
{
    uint16_t raw;

    if (Menu_ADC1SelectChannel(ADC_CHANNEL_7, ADC_SAMPLETIME_239CYCLES_5) != HAL_OK)
    {
        g_sensor.noise_valid = 0u;
        return;
    }

    if (Menu_ADC1ReadAverage(NOISE_SAMPLE_COUNT, &raw) == HAL_OK)
    {
        g_sensor.noise_db = Menu_NoisedBFromRaw(raw);
        g_sensor.noise_valid = 1u;
    }
    else
    {
        g_sensor.noise_valid = 0u;
    }
}

static void Menu_EvaluateAlarms(void)
{
    AlarmState_t prev = g_alarm;
    uint8_t prev_any_alarm;

    prev_any_alarm = (uint8_t)((prev.temp != 0u) ||
                               (prev.humi != 0u) ||
                               (prev.pm25 != 0u) ||
                               (prev.water != 0u) ||
                               (prev.noise != 0u));

    if (g_sensor.temp_humi_valid != 0u)
    {
        g_alarm.temp = Menu_UpdateAlarmWithHysteresis(g_alarm.temp, g_sensor.temp_c, g_threshold.temp_c, g_hysteresis.temp_c);
        g_alarm.humi = Menu_UpdateAlarmWithHysteresis(g_alarm.humi, g_sensor.humi_pct, g_threshold.humi_pct, g_hysteresis.humi_pct);
    }
    else
    {
        g_alarm.temp = 0u;
        g_alarm.humi = 0u;
    }

    if (g_sensor.pm_valid != 0u)
    {
        g_alarm.pm25 = Menu_UpdateAlarmWithHysteresis(g_alarm.pm25, g_sensor.pm25, g_threshold.pm25, g_hysteresis.pm25);
    }
    else
    {
        g_alarm.pm25 = 0u;
    }

    if (g_sensor.water_valid != 0u)
    {
        g_alarm.water = Menu_UpdateAlarmWithHysteresis(g_alarm.water, g_sensor.water_ntu, g_threshold.water_ntu, g_hysteresis.water_ntu);
    }
    else
    {
        g_alarm.water = 0u;
    }

    if (g_sensor.noise_valid != 0u)
    {
        g_alarm.noise = Menu_UpdateAlarmWithHysteresis(g_alarm.noise, g_sensor.noise_db, g_threshold.noise_db, g_hysteresis.noise_db);
    }
    else
    {
        g_alarm.noise = 0u;
    }

    g_flags.any_alarm = (uint8_t)((g_alarm.temp != 0u) ||
                                  (g_alarm.humi != 0u) ||
                                  (g_alarm.pm25 != 0u) ||
                                  (g_alarm.water != 0u) ||
                                  (g_alarm.noise != 0u));

    if (g_flags.any_alarm == 0u)
    {
        g_flags.alarm_page_auto_muted = 0u;
        g_flags.alarm_page_auto_pending = 0u;
    }
    else if ((prev_any_alarm == 0u) && (g_flags.alarm_page_auto_muted == 0u))
    {
        g_flags.alarm_page_auto_pending = 1u;
    }

    Menu_SendAlarmIfChanged("TEMP", prev.temp, g_alarm.temp, g_sensor.temp_c, g_threshold.temp_c);
    Menu_SendAlarmIfChanged("HUMI", prev.humi, g_alarm.humi, g_sensor.humi_pct, g_threshold.humi_pct);
    Menu_SendAlarmIfChanged("PM25", prev.pm25, g_alarm.pm25, g_sensor.pm25, g_threshold.pm25);
    Menu_SendAlarmIfChanged("WATER", prev.water, g_alarm.water, g_sensor.water_ntu, g_threshold.water_ntu);
    Menu_SendAlarmIfChanged("NOISE", prev.noise, g_alarm.noise, g_sensor.noise_db, g_threshold.noise_db);
}

static void Menu_OnThresholdChanged(int32_t new_value)
{
    (void)new_value;
    Menu_EvaluateAlarms();
    Menu_MarkScreenDirty();
}

static void Menu_ServiceRemoteQuery(void)
{
    if ((g_flags.remote_query_pending != 0u) && (g_flags.has_remote_link != 0u))
    {
        g_flags.remote_query_pending = 0u;
        Menu_SendTelemetry(g_flags.remote_link_id);
        Menu_MarkScreenDirty();
    }
}

static void Menu_ServiceBuzzer(void)
{
    uint32_t now = HAL_GetTick();

    if (g_flags.any_alarm == 0u)
    {
        if (g_flags.buzzer_on != 0u)
        {
            Buzzer_Off();
            g_flags.buzzer_on = 0u;
        }
        return;
    }

    if ((now - g_last_buzzer_tick) >= BUZZER_TOGGLE_PERIOD_MS)
    {
        g_last_buzzer_tick = now;
        if (g_flags.buzzer_on == 0u)
        {
            Buzzer_On();
            g_flags.buzzer_on = 1u;
        }
        else
        {
            Buzzer_Off();
            g_flags.buzzer_on = 0u;
        }
    }
}

static void Menu_ServiceAlarmPageAutoEntry(void)
{
    if ((g_flags.alarm_page_auto_pending == 0u) ||
        (g_flags.alarm_page_auto_muted != 0u) ||
        (g_flags.any_alarm == 0u))
    {
        return;
    }

    g_flags.alarm_page_auto_pending = 0u;
    MF_OpenCustomPage(Menu_RenderAlarmPage);
    Menu_MarkScreenDirty();
}

static void Menu_DrawTitle(const char *title)
{
    int text_w;
    int x;

    text_w = (int)strlen(title) * 8;
    x = (128 - text_w) / 2;
    if (x < 0)
    {
        x = 0;
    }

    OLED_ShowString(x, 0, (char *)title, OLED_8X16);
    OLED_DrawLine(0, 16, 128, 16);
}

static void Menu_DrawDetailPage(const char *title,
                                const char *line1,
                                const char *line2,
                                const char *line3,
                                const char *line4,
                                const char *footer)
{
    Menu_DrawTitle(title);
    OLED_Printf(0, 18, OLED_6X8, "%s", line1);
    OLED_Printf(0, 28, OLED_6X8, "%s", line2);
    OLED_Printf(0, 38, OLED_6X8, "%s", line3);
    OLED_Printf(0, 48, OLED_6X8, "%s", line4);
    if ((footer != NULL) && (footer[0] != '\0'))
    {
        OLED_Printf(0, 56, OLED_6X8, "%s", footer);
    }
}

static void Menu_RenderTempHumiPage(KeyEvent_t key, uint8_t *exit_flag)
{
    char line1[24];
    char line2[24];
    char line3[24];
    char line4[24];

    if ((exit_flag != NULL) && (key == KEY_BACK))
    {
        *exit_flag = 1u;
        return;
    }

    if (g_sensor.temp_humi_valid != 0u)
    {
        (void)snprintf(line1, sizeof(line1), "Temp : %d C", g_sensor.temp_c);
        (void)snprintf(line2, sizeof(line2), "Humi : %u %%", (unsigned int)g_sensor.humi_pct);
    }
    else
    {
        (void)snprintf(line1, sizeof(line1), "Temp : NA");
        (void)snprintf(line2, sizeof(line2), "Humi : NA");
    }

    (void)snprintf(line3, sizeof(line3), "TH T : %ld C", (long)g_threshold.temp_c);
    (void)snprintf(line4, sizeof(line4), "TH H : %ld %%", (long)g_threshold.humi_pct);

    Menu_DrawDetailPage(Menu_Text(TXT_ITEM_TEMP_HUMI),
                        line1,
                        line2,
                        line3,
                        line4,
                        Menu_Text(TXT_HINT_BACK));
}

static void Menu_RenderPm25Page(KeyEvent_t key, uint8_t *exit_flag)
{
    char line1[24];
    char line2[24];
    char line3[24];
    char line4[24];

    if ((exit_flag != NULL) && (key == KEY_BACK))
    {
        *exit_flag = 1u;
        return;
    }

    if (g_sensor.pm_valid != 0u)
    {
        (void)snprintf(line1, sizeof(line1), "PM25 : %u", (unsigned int)g_sensor.pm25);
    }
    else
    {
        (void)snprintf(line1, sizeof(line1), "PM25 : NA");
    }

    (void)snprintf(line2, sizeof(line2), "TH   : %ld", (long)g_threshold.pm25);
    (void)snprintf(line3, sizeof(line3), "Stat : %s", (g_alarm.pm25 != 0u) ? Menu_Text(TXT_STATUS_ALERT) : Menu_Text(TXT_STATUS_SAFE));
    (void)snprintf(line4, sizeof(line4), "Msg  : %s", (g_alarm.pm25 != 0u) ? Menu_Text(TXT_ALERT_AIR) : Menu_Text(TXT_STATUS_SAFE));

    Menu_DrawDetailPage(Menu_Text(TXT_ITEM_PM25),
                        line1,
                        line2,
                        line3,
                        line4,
                        Menu_Text(TXT_HINT_BACK));
}

static void Menu_RenderWaterPage(KeyEvent_t key, uint8_t *exit_flag)
{
    char line1[24];
    char line2[24];
    char line3[24];
    char line4[24];

    if ((exit_flag != NULL) && (key == KEY_BACK))
    {
        *exit_flag = 1u;
        return;
    }

    if (g_sensor.water_valid != 0u)
    {
        (void)snprintf(line1, sizeof(line1), "NTU  : %u", (unsigned int)g_sensor.water_ntu);
    }
    else
    {
        (void)snprintf(line1, sizeof(line1), "NTU  : NA");
    }

    (void)snprintf(line2, sizeof(line2), "TH   : %ld", (long)g_threshold.water_ntu);
    (void)snprintf(line3, sizeof(line3), "Volt : %.2fV", g_azdm01_result.voltage);
    (void)snprintf(line4, sizeof(line4), "Stat : %s", (g_alarm.water != 0u) ? Menu_Text(TXT_STATUS_ALERT) : Menu_Text(TXT_STATUS_SAFE));

    Menu_DrawDetailPage(Menu_Text(TXT_ITEM_WATER),
                        line1,
                        line2,
                        line3,
                        line4,
                        Menu_Text(TXT_HINT_BACK));
}

static void Menu_RenderNoisePage(KeyEvent_t key, uint8_t *exit_flag)
{
    char line1[24];
    char line2[24];
    char line3[24];
    char line4[24];

    if ((exit_flag != NULL) && (key == KEY_BACK))
    {
        *exit_flag = 1u;
        return;
    }

    if (g_sensor.noise_valid != 0u)
    {
        (void)snprintf(line1, sizeof(line1), "Noise: %u dB", (unsigned int)g_sensor.noise_db);
    }
    else
    {
        (void)snprintf(line1, sizeof(line1), "Noise: NA");
    }

    (void)snprintf(line2, sizeof(line2), "TH   : %ld dB", (long)g_threshold.noise_db);
    (void)snprintf(line3, sizeof(line3), "BEEP : %s", (g_flags.buzzer_on != 0u) ? Menu_Text(TXT_STATE_ON) : Menu_Text(TXT_STATE_OFF));
    (void)snprintf(line4, sizeof(line4), "Stat : %s", (g_alarm.noise != 0u) ? Menu_Text(TXT_STATUS_ALERT) : Menu_Text(TXT_STATUS_SAFE));

    Menu_DrawDetailPage(Menu_Text(TXT_ITEM_NOISE),
                        line1,
                        line2,
                        line3,
                        line4,
                        Menu_Text(TXT_HINT_BACK));
}

static void Menu_RenderAlarmPage(KeyEvent_t key, uint8_t *exit_flag)
{
    uint8_t y = 18u;

    if ((exit_flag != NULL) && (key == KEY_BACK))
    {
        if (g_flags.any_alarm != 0u)
        {
            g_flags.alarm_page_auto_muted = 1u;
        }
        g_flags.alarm_page_auto_pending = 0u;
        *exit_flag = 1u;
        return;
    }

    Menu_DrawTitle(Menu_Text(TXT_TITLE_ALARM));

    if (g_flags.any_alarm == 0u)
    {
        OLED_Printf(0, 24, OLED_6X8, "No active alarm");
        OLED_Printf(0, 36, OLED_6X8, "System safe");
        OLED_Printf(0, 56, OLED_6X8, "%s", Menu_Text(TXT_HINT_BACK));
        return;
    }

    if ((g_alarm.temp != 0u) && (g_alarm.humi != 0u))
    {
        OLED_Printf(0, y, OLED_6X8, "T:%ld/%ld H:%lu/%ld",
                    (long)g_sensor.temp_c,
                    (long)g_threshold.temp_c,
                    (unsigned long)g_sensor.humi_pct,
                    (long)g_threshold.humi_pct);
        y = (uint8_t)(y + 10u);
    }
    else if (g_alarm.temp != 0u)
    {
        OLED_Printf(0, y, OLED_6X8, "Temp %ld/%ldC",
                    (long)g_sensor.temp_c,
                    (long)g_threshold.temp_c);
        y = (uint8_t)(y + 10u);
    }
    else if (g_alarm.humi != 0u)
    {
        OLED_Printf(0, y, OLED_6X8, "Humi %lu/%ld%%",
                    (unsigned long)g_sensor.humi_pct,
                    (long)g_threshold.humi_pct);
        y = (uint8_t)(y + 10u);
    }

    if (g_alarm.pm25 != 0u)
    {
        OLED_Printf(0, y, OLED_6X8, "PM25 %u/%ld",
                    (unsigned int)g_sensor.pm25,
                    (long)g_threshold.pm25);
        y = (uint8_t)(y + 10u);
    }

    if (g_alarm.water != 0u)
    {
        OLED_Printf(0, y, OLED_6X8, "Water %u/%ld",
                    (unsigned int)g_sensor.water_ntu,
                    (long)g_threshold.water_ntu);
        y = (uint8_t)(y + 10u);
    }

    if (g_alarm.noise != 0u)
    {
        OLED_Printf(0, y, OLED_6X8, "Noise %u/%lddB",
                    (unsigned int)g_sensor.noise_db,
                    (long)g_threshold.noise_db);
    }

    OLED_Printf(0, 56, OLED_6X8, "%s", Menu_Text(TXT_HINT_BACK));
}

static void Menu_RenderEspStatePage(KeyEvent_t key, uint8_t *exit_flag)
{
    char line1[24];
    char line2[24];
    char line3[24];
    char line4[24];

    if ((exit_flag != NULL) && (key == KEY_BACK))
    {
        *exit_flag = 1u;
        return;
    }

    (void)snprintf(line1, sizeof(line1), "Init : %s", (g_flags.esp_ready != 0u) ? Menu_Text(TXT_STATE_READY) : Menu_Text(TXT_STATE_FAIL));
    (void)snprintf(line2, sizeof(line2), "Link : %s", (g_flags.has_remote_link != 0u) ? Menu_Text(TXT_STATE_LINKED) : Menu_Text(TXT_STATE_IDLE));
    (void)snprintf(line3, sizeof(line3), "ID/RX: %u/%lu",
                   (unsigned int)g_flags.remote_link_id,
                   (unsigned long)g_flags.rx_cmd_count);
    (void)snprintf(line4, sizeof(line4), "TX/BD: %lu/%lu",
                   (unsigned long)g_flags.tx_frame_count,
                   (unsigned long)g_flags.invalid_cmd_count);

    Menu_DrawDetailPage(Menu_Text(TXT_ITEM_ESP_STATE),
                        line1,
                        line2,
                        line3,
                        line4,
                        Menu_Text(TXT_HINT_BACK));
}

static void Menu_RenderCommandLogPage(KeyEvent_t key, uint8_t *exit_flag)
{
    uint8_t i;

    if ((exit_flag != NULL) && (key == KEY_BACK))
    {
        *exit_flag = 1u;
        return;
    }

    Menu_DrawTitle(Menu_Text(TXT_ITEM_CMD_LOG));
    if (g_command_log.count == 0u)
    {
        OLED_Printf(0, 26, OLED_6X8, "%s", Menu_Text(TXT_HINT_EMPTY));
        OLED_Printf(0, 56, OLED_6X8, "%s", Menu_Text(TXT_HINT_BACK));
        return;
    }

    for (i = 0u; (i < g_command_log.count) && (i < COMMAND_LOG_DEPTH); i++)
    {
        OLED_Printf(0, 20 + ((int)i * 8), OLED_6X8, "%u:%s",
                    (unsigned int)(i + 1u),
                    g_command_log.entries[i]);
    }
}

static void Menu_BuildTree(void)
{
    MF_Reset();

    g_root_menu = MF_CreateMenu(Menu_Text(TXT_TITLE_MAIN));
    g_sensor_menu = MF_CreateMenu(Menu_Text(TXT_TITLE_SENSOR));
    g_threshold_menu = MF_CreateMenu(Menu_Text(TXT_TITLE_THRESHOLD));
    g_esp_menu = MF_CreateMenu(Menu_Text(TXT_TITLE_ESP));

    if ((g_root_menu == NULL) || (g_sensor_menu == NULL) ||
        (g_threshold_menu == NULL) || (g_esp_menu == NULL))
    {
        Menu_SystemErrorHandler();
    }

    MF_AddSubmenu(g_root_menu, Menu_Text(TXT_MENU_VIEW), g_sensor_menu);
    MF_AddSubmenu(g_root_menu, Menu_Text(TXT_MENU_THRESHOLD), g_threshold_menu);
    MF_AddSubmenu(g_root_menu, Menu_Text(TXT_MENU_ESP), g_esp_menu);
    MF_AddCustomPage(g_root_menu, Menu_Text(TXT_MENU_ALARM), Menu_RenderAlarmPage);

    MF_AddCustomPage(g_sensor_menu, Menu_Text(TXT_ITEM_TEMP_HUMI), Menu_RenderTempHumiPage);
    MF_AddCustomPage(g_sensor_menu, Menu_Text(TXT_ITEM_PM25), Menu_RenderPm25Page);
    MF_AddCustomPage(g_sensor_menu, Menu_Text(TXT_ITEM_WATER), Menu_RenderWaterPage);
    MF_AddCustomPage(g_sensor_menu, Menu_Text(TXT_ITEM_NOISE), Menu_RenderNoisePage);

    MF_AddValue(g_threshold_menu, Menu_Text(TXT_ITEM_TEMP_TH), &g_threshold.temp_c, TH_TEMP_MIN, TH_TEMP_MAX, TH_TEMP_STEP, "C", Menu_OnThresholdChanged);
    MF_AddValue(g_threshold_menu, Menu_Text(TXT_ITEM_HUMI_TH), &g_threshold.humi_pct, TH_HUMI_MIN, TH_HUMI_MAX, TH_HUMI_STEP, "%", Menu_OnThresholdChanged);
    MF_AddValue(g_threshold_menu, Menu_Text(TXT_ITEM_PM25_TH), &g_threshold.pm25, TH_PM25_MIN, TH_PM25_MAX, TH_PM25_STEP, "", Menu_OnThresholdChanged);
    MF_AddValue(g_threshold_menu, Menu_Text(TXT_ITEM_WATER_TH), &g_threshold.water_ntu, TH_WATER_MIN, TH_WATER_MAX, TH_WATER_STEP, "", Menu_OnThresholdChanged);
    MF_AddValue(g_threshold_menu, Menu_Text(TXT_ITEM_NOISE_TH), &g_threshold.noise_db, TH_NOISE_MIN, TH_NOISE_MAX, TH_NOISE_STEP, "dB", Menu_OnThresholdChanged);

    MF_AddCustomPage(g_esp_menu, Menu_Text(TXT_ITEM_ESP_STATE), Menu_RenderEspStatePage);
    MF_AddCustomPage(g_esp_menu, Menu_Text(TXT_ITEM_CMD_LOG), Menu_RenderCommandLogPage);
}

static void Menu_DoInitialSample(void)
{
    Menu_SampleTempHumi();
    Menu_SamplePm25();
    Menu_SampleWaterNtu();
    Menu_SampleNoiseDb();
    Menu_EvaluateAlarms();
}

static void Menu_ProcessKeyEvents(void)
{
    KeyEvent_t ev;

    while ((ev = Key_Scan()) != KEY_NONE)
    {
        MF_Process(ev);
        Menu_MarkScreenDirty();
    }
}

static void Menu_ServicePeriodicTasks(void)
{
    uint32_t now = HAL_GetTick();

    if ((now - g_last_dht_tick) >= SENSOR_DHT_PERIOD_MS)
    {
        g_last_dht_tick = now;
        Menu_SampleTempHumi();
        Menu_EvaluateAlarms();
        Menu_MarkScreenDirty();
    }

    if ((now - g_last_env_tick) >= SENSOR_ENV_PERIOD_MS)
    {
        g_last_env_tick = now;
        Menu_SamplePm25();
        Menu_SampleWaterNtu();
        Menu_SampleNoiseDb();
        Menu_EvaluateAlarms();
        Menu_MarkScreenDirty();
    }

    Menu_ServiceBuzzer();

    if ((now - g_last_screen_tick) >= SCREEN_REFRESH_PERIOD_MS)
    {
        g_last_screen_tick = now;
        Menu_MarkScreenDirty();
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if ((huart != NULL) && (huart->Instance == USART1))
    {
        ESP_RxCallback();
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if ((huart != NULL) && (huart->Instance == USART1))
    {
        ESP_ErrorCallback(huart);
    }
}

void Menu_AppInit(void)
{
    uint32_t now;

    OLED_Init();
    Key_Init();
    Buzzer_Init();
    Buzzer_Off();

    Menu_MX_ADC1_Init();
    DHT11_Init();
    DC01_Init(9600u);

    if (AZDM01_Init(&g_azdm01, &hadc1, NULL, 0u) != HAL_OK)
    {
        Menu_SystemErrorHandler();
    }

    AZDM01_SetVref(&g_azdm01, 3.3f * AZDM01_PA6_TO_OUTPUT_SCALE);
    g_azdm01.digitalPort = NULL;
    g_azdm01.digitalPin = 0u;

    if (ESP_Init() == ESP_OK)
    {
        g_flags.esp_ready = 1u;
        ESP_RegisterCallback(Menu_ESPUserPacketCallback);
    }
    else
    {
        g_flags.esp_ready = 0u;
    }

    Menu_BuildTree();
    MF_SetCursorStyle(MF_CURSOR_BOX);
    MF_Start(g_root_menu);

    now = HAL_GetTick();
    g_last_env_tick = now;
    g_last_dht_tick = now;
    g_last_screen_tick = now;
    g_last_buzzer_tick = now;
    g_last_pm_success_tick = now;
    g_pm_start_tick = now;

    Menu_DoInitialSample();
    Menu_ServiceAlarmPageAutoEntry();
    Menu_MarkScreenDirty();
    MF_Render();
    g_screen_dirty = 0u;
}

void Menu_AppLoop(void)
{
    Menu_ProcessKeyEvents();

    if (g_flags.esp_ready != 0u)
    {
        ESP_Process();
    }

    Menu_ServiceRemoteQuery();
    Menu_ServicePeriodicTasks();
    Menu_ServiceAlarmPageAutoEntry();

    if (g_screen_dirty != 0u)
    {
        MF_Render();
        g_screen_dirty = MF_IsAnimating();
    }

    delay_ms(APP_LOOP_DELAY_MS);
}
