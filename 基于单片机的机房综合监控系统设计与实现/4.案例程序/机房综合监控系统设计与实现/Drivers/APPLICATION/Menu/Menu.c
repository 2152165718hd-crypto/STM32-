#include "APPLICATION/Menu/Menu.h"
#include "APPLICATION/Menu/MenuFramework.h"
#include "Hardware/Buzzer/Buzzer.h"
#include "Hardware/CDCM/CDCM.h"
#include "Hardware/DHT11/DHT11.h"
#include "Hardware/Flame/Flame.h"
#include "Hardware/KEY/KEY.h"
#include "Hardware/MQ2_Smoke/MQ2_Smoke.h"
#include "Hardware/OLED/OLED.h"
#include "Hardware/SR505/SR505.h"
#include "Hardware/Water_Sensor/Water_Sensor.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define MENU_FAST_SAMPLE_PERIOD_MS 200U
#define MENU_DHT_SAMPLE_PERIOD_MS 2000U
#define MENU_UI_FRAME_PERIOD_MS 25U

#define MENU_ADC_MAX_VALUE 4095U
#define MENU_ALARM_PAGE_LINES 5U
#define MENU_SCREEN_WIDTH 128U
#define MENU_HUMAN_ALARM_LATCH_MS 3000U
#define MENU_CURRENT_ZERO_CAL_SAMPLES 32U
#define MENU_CURRENT_FILTER_SHIFT 2U
#define MENU_CURRENT_ZERO_TRACK_SHIFT 5U
#define MENU_CURRENT_ZERO_TRACK_BAND_ADC 24U
#define MENU_CURRENT_ZERO_DEADBAND_ADC 12U

#define MENU_DEFAULT_SMOKE_THRESHOLD_PERCENT 60
#define MENU_DEFAULT_TEMP_THRESHOLD_C 35
#define MENU_DEFAULT_HUMI_THRESHOLD_PERCENT 80
#define MENU_DEFAULT_CURRENT_THRESHOLD_DECI_A 20
#define MENU_DEFAULT_WATER_THRESHOLD_PERCENT 30

typedef enum
{
    MENU_ALARM_SMOKE = 1U << 0,
    MENU_ALARM_TEMP = 1U << 1,
    MENU_ALARM_HUMI = 1U << 2,
    MENU_ALARM_CURRENT = 1U << 3,
    MENU_ALARM_WATER = 1U << 4,
    MENU_ALARM_HUMAN = 1U << 5,
    MENU_ALARM_FLAME = 1U << 6,
} MenuAlarmMask_t;

typedef struct
{
    uint16_t smoke_adc_raw;
    uint8_t smoke_percent;
    uint8_t smoke_do;

    uint8_t temperature_c;
    uint8_t humidity_percent;
    uint8_t dht_status;
    uint8_t dht_valid;

    uint16_t current_adc_raw;
    uint16_t current_adc_filtered;
    uint16_t current_zero_adc_raw;
    uint16_t current_module_mv;
    uint16_t current_zero_module_mv;
    int32_t current_deci_a;
    uint8_t current_do;

    uint16_t water_adc_raw;
    uint8_t water_percent;

    uint8_t human_level;
    uint8_t human_alarm_active;

    uint16_t flame_adc_raw;
    uint8_t flame_do;
    uint8_t flame_ao_percent;

    int32_t smoke_threshold_percent;
    int32_t temp_threshold_c;
    int32_t humi_threshold_percent;
    int32_t current_threshold_deci_a;
    int32_t water_threshold_percent;

    uint8_t current_alarm_mask;
    uint8_t prev_alarm_mask;
    uint8_t alarm_auto_enter_latched;
    uint8_t alarm_page_scroll;
    uint8_t overall_alarm;
    uint8_t ui_dirty;

    uint32_t last_fast_sample_ms;
    uint32_t last_dht_sample_ms;
    uint32_t last_ui_frame_ms;
    uint32_t human_alarm_last_trigger_ms;
} MenuAppState_t;

extern DHT11_Data_t DHT11_Data;

static MenuAppState_t g_menu_state;

static MF_Menu_t *g_root_menu = NULL;
static MF_Menu_t *g_sensor_menu = NULL;
static MF_Menu_t *g_threshold_menu = NULL;
static MF_Menu_t *g_smoke_threshold_menu = NULL;
static MF_Menu_t *g_temp_threshold_menu = NULL;
static MF_Menu_t *g_humi_threshold_menu = NULL;
static MF_Menu_t *g_current_threshold_menu = NULL;
static MF_Menu_t *g_water_threshold_menu = NULL;

static void Menu_OnThresholdChanged(int32_t new_value);

static void Menu_PageSmoke(KeyEvent_t key, uint8_t *exit_flag);
static void Menu_PageTempHumi(KeyEvent_t key, uint8_t *exit_flag);
static void Menu_PageCurrent(KeyEvent_t key, uint8_t *exit_flag);
static void Menu_PageWater(KeyEvent_t key, uint8_t *exit_flag);
static void Menu_PageHuman(KeyEvent_t key, uint8_t *exit_flag);
static void Menu_PageFlame(KeyEvent_t key, uint8_t *exit_flag);
static void Menu_PageAlarmInfo(KeyEvent_t key, uint8_t *exit_flag);

static void Menu_LoadDefaults(void);
static void Menu_BuildMenus(void);
static void Menu_CalibrateCurrentZero(void);
static void Menu_SampleFastSensors(void);
static void Menu_SampleDhtSensor(void);
static void Menu_UpdateAlarmState(void);
static void Menu_ClampAlarmScroll(void);

static const uint8_t g_alarm_order_masks[] =
{
    MENU_ALARM_SMOKE,
    MENU_ALARM_TEMP,
    MENU_ALARM_HUMI,
    MENU_ALARM_CURRENT,
    MENU_ALARM_WATER,
    MENU_ALARM_HUMAN,
    MENU_ALARM_FLAME,
};

static const char *const g_alarm_texts[] =
{
    "Smoke High",
    "Temp High",
    "Humi High",
    "Current High",
    "Water Leak",
    "Human Intrusion",
    "Flame Alarm",
};

static uint8_t Menu_MapAdcToPercent(uint16_t adc_raw)
{
    uint32_t percent;

    percent = ((uint32_t)adc_raw * 100U + (MENU_ADC_MAX_VALUE / 2U)) / MENU_ADC_MAX_VALUE;
    if (percent > 100U)
    {
        percent = 100U;
    }

    return (uint8_t)percent;
}

static int32_t Menu_CurrentToDeciAmp(float current_a)
{
    int32_t deci_amp;

    if (current_a < 0.0f)
    {
        current_a = -current_a;
    }

    deci_amp = (int32_t)(current_a * 10.0f + 0.5f);
    if (deci_amp < 0)
    {
        deci_amp = 0;
    }
    if (deci_amp > 999)
    {
        deci_amp = 999;
    }

    return deci_amp;
}

static uint16_t Menu_AbsDiffU16(uint16_t lhs, uint16_t rhs)
{
    return (lhs >= rhs) ? (uint16_t)(lhs - rhs) : (uint16_t)(rhs - lhs);
}

static uint16_t Menu_MoveTowardU16(uint16_t current, uint16_t target, uint8_t shift)
{
    int32_t delta;
    int32_t next;

    if (shift == 0U)
    {
        return target;
    }

    delta = (int32_t)target - (int32_t)current;
    if (delta == 0)
    {
        return current;
    }

    if (delta > 0)
    {
        int32_t step = delta >> shift;
        if (step == 0)
        {
            step = 1;
        }
        next = (int32_t)current + step;
    }
    else
    {
        int32_t step = (-delta) >> shift;
        if (step == 0)
        {
            step = 1;
        }
        next = (int32_t)current - step;
    }

    if (next < 0)
    {
        next = 0;
    }
    if (next > 4095)
    {
        next = 4095;
    }

    return (uint16_t)next;
}

static uint16_t Menu_CurrentRawToModuleMv(uint16_t adc_raw)
{
    float adc_voltage;
    float module_voltage;
    uint32_t module_mv;

    adc_voltage = ((float)adc_raw / CDCM_ADC_MAX) * CDCM_ADC_VREF;
    module_voltage = adc_voltage / CDCM_AO_DIVIDER_RATIO;
    module_mv = (uint32_t)(module_voltage * 1000.0f + 0.5f);

    if (module_mv > 9999U)
    {
        module_mv = 9999U;
    }

    return (uint16_t)module_mv;
}

static uint8_t Menu_FlameRawToPercent(uint16_t adc_raw)
{
    uint32_t percent;

    percent = ((uint32_t)(MENU_ADC_MAX_VALUE - adc_raw) * 100U + (MENU_ADC_MAX_VALUE / 2U)) / MENU_ADC_MAX_VALUE;
    if (percent > 100U)
    {
        percent = 100U;
    }

    return (uint8_t)percent;
}

static int32_t Menu_CurrentRawDiffToDeciAmp(uint16_t adc_raw, uint16_t zero_adc_raw)
{
    float adc_voltage_diff;
    float module_voltage_diff;
    float current_a;
    uint16_t adc_diff;

    adc_diff = Menu_AbsDiffU16(adc_raw, zero_adc_raw);
    if (adc_diff <= MENU_CURRENT_ZERO_DEADBAND_ADC)
    {
        return 0;
    }

    adc_voltage_diff = ((float)adc_diff / CDCM_ADC_MAX) * CDCM_ADC_VREF;
    module_voltage_diff = adc_voltage_diff / CDCM_AO_DIVIDER_RATIO;
    current_a = module_voltage_diff / CDCM_SENSITIVITY_V_PER_A;

    return Menu_CurrentToDeciAmp(current_a);
}

static void Menu_FormatDeciAmp(char *buf, size_t buf_size, int32_t deci_amp)
{
    int32_t abs_value;

    if ((buf == NULL) || (buf_size == 0U))
    {
        return;
    }

    if (deci_amp < 0)
    {
        abs_value = -deci_amp;
        snprintf(buf, buf_size, "-%ld.%ldA",
                 (long)(abs_value / 10),
                 (long)(abs_value % 10));
    }
    else
    {
        snprintf(buf, buf_size, "%ld.%ldA",
                 (long)(deci_amp / 10),
                 (long)(deci_amp % 10));
    }
}

static uint8_t Menu_GetFontWidth(uint8_t font)
{
    return (font == OLED_8X16) ? 8U : 6U;
}

static uint8_t Menu_GetStringPixelWidth(const char *text, uint8_t font)
{
    uint16_t len;

    len = 0U;
    while ((text != NULL) && (text[len] != '\0'))
    {
        ++len;
    }

    return (uint8_t)(len * Menu_GetFontWidth(font));
}

static void Menu_DrawCenteredText(uint8_t y, uint8_t font, const char *text)
{
    int16_t x;
    uint8_t width;

    if (text == NULL)
    {
        text = "";
    }

    width = Menu_GetStringPixelWidth(text, font);
    x = ((int16_t)MENU_SCREEN_WIDTH - (int16_t)width) / 2;
    if (x < 0)
    {
        x = 0;
    }

    OLED_ShowString(x, y, (char *)text, font);
}

static void Menu_PrintAt(uint8_t x, uint8_t y, uint8_t font, const char *fmt, ...)
{
    char text[32];
    va_list args;

    if (fmt == NULL)
    {
        return;
    }

    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);

    OLED_ShowString(x, y, text, font);
}

static uint8_t Menu_IsAlarmActive(uint8_t alarm_mask)
{
    return ((g_menu_state.current_alarm_mask & alarm_mask) != 0U) ? 1U : 0U;
}

static uint8_t Menu_GetAlarmCount(void)
{
    uint8_t count;
    uint8_t index;

    count = 0U;
    for (index = 0U; index < (sizeof(g_alarm_order_masks) / sizeof(g_alarm_order_masks[0])); ++index)
    {
        if ((g_menu_state.current_alarm_mask & g_alarm_order_masks[index]) != 0U)
        {
            ++count;
        }
    }

    return count;
}

static const char *Menu_GetActiveAlarmText(uint8_t active_index)
{
    uint8_t count;
    uint8_t index;

    count = 0U;
    for (index = 0U; index < (sizeof(g_alarm_order_masks) / sizeof(g_alarm_order_masks[0])); ++index)
    {
        if ((g_menu_state.current_alarm_mask & g_alarm_order_masks[index]) != 0U)
        {
            if (count == active_index)
            {
                return g_alarm_texts[index];
            }
            ++count;
        }
    }

    return "";
}

static void Menu_ClampAlarmScroll(void)
{
    uint8_t alarm_count;
    uint8_t max_start;

    alarm_count = Menu_GetAlarmCount();
    max_start = 0U;

    if (alarm_count > MENU_ALARM_PAGE_LINES)
    {
        max_start = (uint8_t)(((alarm_count - 1U) / MENU_ALARM_PAGE_LINES) * MENU_ALARM_PAGE_LINES);
    }

    if (g_menu_state.alarm_page_scroll > max_start)
    {
        g_menu_state.alarm_page_scroll = max_start;
    }
}

static void Menu_LoadDefaults(void)
{
    memset(&g_menu_state, 0, sizeof(g_menu_state));

    g_menu_state.smoke_threshold_percent = MENU_DEFAULT_SMOKE_THRESHOLD_PERCENT;
    g_menu_state.temp_threshold_c = MENU_DEFAULT_TEMP_THRESHOLD_C;
    g_menu_state.humi_threshold_percent = MENU_DEFAULT_HUMI_THRESHOLD_PERCENT;
    g_menu_state.current_threshold_deci_a = MENU_DEFAULT_CURRENT_THRESHOLD_DECI_A;
    g_menu_state.water_threshold_percent = MENU_DEFAULT_WATER_THRESHOLD_PERCENT;
    g_menu_state.dht_status = DHT11_STATUS_NO_RESPONSE;
}

static void Menu_BuildMenus(void)
{
    MF_Reset();

    g_root_menu = MF_CreateMenu("Main Menu");
    g_sensor_menu = MF_CreateMenu("Sensor Data");
    g_threshold_menu = MF_CreateMenu("Env Threshold");
    g_smoke_threshold_menu = MF_CreateMenu("Smoke TH");
    g_temp_threshold_menu = MF_CreateMenu("Temp TH");
    g_humi_threshold_menu = MF_CreateMenu("Humi TH");
    g_current_threshold_menu = MF_CreateMenu("Current TH");
    g_water_threshold_menu = MF_CreateMenu("Water TH");

    MF_AddSubmenu(g_root_menu, "Sensor Data", g_sensor_menu);
    MF_AddCustomPage(g_root_menu, "Alarm Info", Menu_PageAlarmInfo);
    MF_AddSubmenu(g_root_menu, "Env Threshold", g_threshold_menu);

    MF_AddCustomPage(g_sensor_menu, "Smoke", Menu_PageSmoke);
    MF_AddCustomPage(g_sensor_menu, "Temp&Humi", Menu_PageTempHumi);
    MF_AddCustomPage(g_sensor_menu, "Current", Menu_PageCurrent);
    MF_AddCustomPage(g_sensor_menu, "Water", Menu_PageWater);
    MF_AddCustomPage(g_sensor_menu, "Human", Menu_PageHuman);
    MF_AddCustomPage(g_sensor_menu, "Flame", Menu_PageFlame);

    MF_AddSubmenu(g_threshold_menu, "Smoke", g_smoke_threshold_menu);
    MF_AddSubmenu(g_threshold_menu, "Temp", g_temp_threshold_menu);
    MF_AddSubmenu(g_threshold_menu, "Humi", g_humi_threshold_menu);
    MF_AddSubmenu(g_threshold_menu, "Current", g_current_threshold_menu);
    MF_AddSubmenu(g_threshold_menu, "Water", g_water_threshold_menu);

    MF_AddValue(g_smoke_threshold_menu, "Threshold",
                &g_menu_state.smoke_threshold_percent, 0, 100, 1, "%", Menu_OnThresholdChanged);
    MF_AddValue(g_temp_threshold_menu, "Threshold",
                &g_menu_state.temp_threshold_c, 0, 80, 1, "C", Menu_OnThresholdChanged);
    MF_AddValue(g_humi_threshold_menu, "Threshold",
                &g_menu_state.humi_threshold_percent, 0, 100, 1, "%", Menu_OnThresholdChanged);
    MF_AddValueEx(g_current_threshold_menu, "Threshold",
                  &g_menu_state.current_threshold_deci_a, 0, 350, 5, 10, 1, "A", Menu_OnThresholdChanged);
    MF_AddValue(g_water_threshold_menu, "Threshold",
                &g_menu_state.water_threshold_percent, 0, 100, 1, "%", Menu_OnThresholdChanged);

    MF_SetCursorStyle(MF_CURSOR_REVERSE);
    MF_Start(g_root_menu);
}

static void Menu_CalibrateCurrentZero(void)
{
    uint32_t sum;
    uint16_t sample_index;
    uint16_t average;

    sum = 0U;

    for (sample_index = 0U; sample_index < MENU_CURRENT_ZERO_CAL_SAMPLES; ++sample_index)
    {
        sum += CDCM_GetAO_Raw();
        HAL_Delay(2);
    }

    average = (uint16_t)((sum + (MENU_CURRENT_ZERO_CAL_SAMPLES / 2U)) / MENU_CURRENT_ZERO_CAL_SAMPLES);

    g_menu_state.current_adc_raw = average;
    g_menu_state.current_adc_filtered = average;
    g_menu_state.current_zero_adc_raw = average;
    g_menu_state.current_module_mv = Menu_CurrentRawToModuleMv(average);
    g_menu_state.current_zero_module_mv = g_menu_state.current_module_mv;
    g_menu_state.current_deci_a = 0;
}

static void Menu_SampleFastSensors(void)
{
    uint32_t now;

    now = HAL_GetTick();

    g_menu_state.smoke_adc_raw = MQ2_Smoke_GetAO_Raw();
    g_menu_state.smoke_percent = Menu_MapAdcToPercent(g_menu_state.smoke_adc_raw);
    g_menu_state.smoke_do = MQ2_Smoke_GetDO();

    g_menu_state.water_adc_raw = Water_Sensor_GetAO_Raw();
    g_menu_state.water_percent = Menu_MapAdcToPercent(g_menu_state.water_adc_raw);

    g_menu_state.current_adc_raw = CDCM_GetAO_Raw();
    if (g_menu_state.current_adc_filtered == 0U)
    {
        g_menu_state.current_adc_filtered = g_menu_state.current_adc_raw;
    }
    else
    {
        g_menu_state.current_adc_filtered = Menu_MoveTowardU16(
            g_menu_state.current_adc_filtered,
            g_menu_state.current_adc_raw,
            MENU_CURRENT_FILTER_SHIFT);
    }

    if (Menu_AbsDiffU16(g_menu_state.current_adc_filtered, g_menu_state.current_zero_adc_raw) <=
        MENU_CURRENT_ZERO_TRACK_BAND_ADC)
    {
        g_menu_state.current_zero_adc_raw = Menu_MoveTowardU16(
            g_menu_state.current_zero_adc_raw,
            g_menu_state.current_adc_filtered,
            MENU_CURRENT_ZERO_TRACK_SHIFT);
    }

    g_menu_state.current_module_mv = Menu_CurrentRawToModuleMv(g_menu_state.current_adc_filtered);
    g_menu_state.current_zero_module_mv = Menu_CurrentRawToModuleMv(g_menu_state.current_zero_adc_raw);
    g_menu_state.current_do = CDCM_GetDO();
    g_menu_state.current_deci_a = Menu_CurrentRawDiffToDeciAmp(
        g_menu_state.current_adc_filtered,
        g_menu_state.current_zero_adc_raw);

    SR505_Update();
    g_menu_state.human_level = SR505_GetLevel();
    if (SR505_GetMotionStartEvent() != 0U)
    {
        g_menu_state.human_alarm_active = 1U;
        g_menu_state.human_alarm_last_trigger_ms = now;
    }
    else if ((g_menu_state.human_alarm_active != 0U) &&
             ((now - g_menu_state.human_alarm_last_trigger_ms) >= MENU_HUMAN_ALARM_LATCH_MS))
    {
        g_menu_state.human_alarm_active = 0U;
    }
    (void)SR505_GetMotionStopEvent();

    g_menu_state.flame_do = Flame_GetDO();
    g_menu_state.flame_adc_raw = Flame_GetAO_Raw();
    g_menu_state.flame_ao_percent = Menu_FlameRawToPercent(g_menu_state.flame_adc_raw);
}

static void Menu_SampleDhtSensor(void)
{
    uint8_t status;

    status = DHT11_ReadData();
    g_menu_state.dht_status = status;

    if (status == DHT11_STATUS_OK)
    {
        g_menu_state.temperature_c = DHT11_Data.temp_int;
        g_menu_state.humidity_percent = DHT11_Data.humi_int;
        g_menu_state.dht_valid = 1U;
    }
}

static void Menu_UpdateAlarmState(void)
{
    uint8_t mask;
    uint8_t newly_triggered;

    mask = 0U;

    if (g_menu_state.smoke_percent >= g_menu_state.smoke_threshold_percent)
    {
        mask |= MENU_ALARM_SMOKE;
    }
    if ((g_menu_state.dht_valid != 0U) && (g_menu_state.temperature_c >= g_menu_state.temp_threshold_c))
    {
        mask |= MENU_ALARM_TEMP;
    }
    if ((g_menu_state.dht_valid != 0U) && (g_menu_state.humidity_percent >= g_menu_state.humi_threshold_percent))
    {
        mask |= MENU_ALARM_HUMI;
    }
    if (g_menu_state.current_deci_a >= g_menu_state.current_threshold_deci_a)
    {
        mask |= MENU_ALARM_CURRENT;
    }
    if (g_menu_state.water_percent >= g_menu_state.water_threshold_percent)
    {
        mask |= MENU_ALARM_WATER;
    }
    if (g_menu_state.human_alarm_active != 0U)
    {
        mask |= MENU_ALARM_HUMAN;
    }
    if (g_menu_state.flame_do != 0U)
    {
        mask |= MENU_ALARM_FLAME;
    }

    g_menu_state.current_alarm_mask = mask;
    g_menu_state.overall_alarm = (mask != 0U) ? 1U : 0U;

    if (g_menu_state.overall_alarm != 0U)
    {
        Buzzer_On();
    }
    else
    {
        Buzzer_Off();
    }

    Menu_ClampAlarmScroll();

    if (mask == 0U)
    {
        g_menu_state.alarm_auto_enter_latched = 0U;
        g_menu_state.alarm_page_scroll = 0U;
    }
    else
    {
        newly_triggered = (uint8_t)(mask & (uint8_t)(~g_menu_state.prev_alarm_mask));
        if (newly_triggered != 0U)
        {
            if (MF_IsCustomPageActive(Menu_PageAlarmInfo) != 0U)
            {
                g_menu_state.alarm_auto_enter_latched = 1U;
            }
            else if (g_menu_state.alarm_auto_enter_latched == 0U)
            {
                g_menu_state.alarm_page_scroll = 0U;
                MF_PushOverlayCustomPage(Menu_PageAlarmInfo);
                g_menu_state.alarm_auto_enter_latched = 1U;
                g_menu_state.ui_dirty = 1U;
            }
        }
    }

    g_menu_state.prev_alarm_mask = mask;
}

static void Menu_OnThresholdChanged(int32_t new_value)
{
    (void)new_value;

    Menu_UpdateAlarmState();
    g_menu_state.ui_dirty = 1U;
}

static void Menu_HandleBackKey(KeyEvent_t key, uint8_t *exit_flag)
{
    if ((key == KEY_BACK) && (exit_flag != NULL))
    {
        *exit_flag = 1U;
    }
}

static void Menu_PageSmoke(KeyEvent_t key, uint8_t *exit_flag)
{
    char current_text[16];
    char threshold_text[16];

    Menu_HandleBackKey(key, exit_flag);
    if (key != KEY_NONE)
    {
        return;
    }

    snprintf(current_text, sizeof(current_text), "%u%%", (unsigned)g_menu_state.smoke_percent);
    snprintf(threshold_text, sizeof(threshold_text), "TH %ld%%", (long)g_menu_state.smoke_threshold_percent);

    Menu_DrawCenteredText(0, OLED_6X8, "Smoke");
    Menu_DrawCenteredText(10, OLED_8X16, current_text);
    Menu_DrawCenteredText(28, OLED_8X16, threshold_text);
    Menu_PrintAt(0, 48, OLED_6X8, "State:%s DO:%u",
                 Menu_IsAlarmActive(MENU_ALARM_SMOKE) ? "ALARM" : "NORM",
                 (unsigned)g_menu_state.smoke_do);
    Menu_PrintAt(0, 56, OLED_6X8, "ADC:%4u B:Back", (unsigned)g_menu_state.smoke_adc_raw);
}

static void Menu_PageTempHumi(KeyEvent_t key, uint8_t *exit_flag)
{
    char temp_text[16];
    char humi_text[16];

    Menu_HandleBackKey(key, exit_flag);
    if (key != KEY_NONE)
    {
        return;
    }

    snprintf(temp_text, sizeof(temp_text), "%uC", (unsigned)g_menu_state.temperature_c);
    snprintf(humi_text, sizeof(humi_text), "%u%%", (unsigned)g_menu_state.humidity_percent);

    Menu_DrawCenteredText(0, OLED_6X8, "Temp&Humi");
    Menu_DrawCenteredText(10, OLED_8X16, temp_text);
    Menu_DrawCenteredText(28, OLED_8X16, humi_text);
    Menu_PrintAt(0, 48, OLED_6X8, "TH:%ldC/%ld%%",
                 (long)g_menu_state.temp_threshold_c,
                 (long)g_menu_state.humi_threshold_percent);
    Menu_PrintAt(0, 56, OLED_6X8, "DHT:%s B:Back",
                 (g_menu_state.dht_status == DHT11_STATUS_OK) ? "OK" : "ERR");
}

static void Menu_PageCurrent(KeyEvent_t key, uint8_t *exit_flag)
{
    char current_text[16];
    char threshold_text[16];
    char threshold_line[16];
    char adc_line[24];
    char ao_line[24];
    char status_line[24];
    char state_char;

    Menu_HandleBackKey(key, exit_flag);
    if (key != KEY_NONE)
    {
        return;
    }

    Menu_FormatDeciAmp(current_text, sizeof(current_text), g_menu_state.current_deci_a);
    Menu_FormatDeciAmp(threshold_text, sizeof(threshold_text), g_menu_state.current_threshold_deci_a);
    snprintf(threshold_line, sizeof(threshold_line), "TH %s", threshold_text);
    snprintf(adc_line, sizeof(adc_line), "A:%4u F:%4u Z:%4u",
             (unsigned)g_menu_state.current_adc_raw,
             (unsigned)g_menu_state.current_adc_filtered,
             (unsigned)g_menu_state.current_zero_adc_raw);
    snprintf(ao_line, sizeof(ao_line), "AO:%u.%02uV Z:%u.%02uV",
             (unsigned)(g_menu_state.current_module_mv / 1000U),
             (unsigned)((g_menu_state.current_module_mv % 1000U) / 10U),
             (unsigned)(g_menu_state.current_zero_module_mv / 1000U),
             (unsigned)((g_menu_state.current_zero_module_mv % 1000U) / 10U));
    state_char = Menu_IsAlarmActive(MENU_ALARM_CURRENT) ? 'A' : 'N';
    snprintf(status_line, sizeof(status_line), "S:%c D:%u B:Back",
             state_char,
             (unsigned)g_menu_state.current_do);

    Menu_DrawCenteredText(0, OLED_6X8, "Current");
    Menu_DrawCenteredText(8, OLED_8X16, current_text);
    Menu_DrawCenteredText(24, OLED_8X16, threshold_line);
    Menu_PrintAt(0, 40, OLED_6X8, "%s", adc_line);
    Menu_PrintAt(0, 48, OLED_6X8, "%s", ao_line);
    Menu_PrintAt(0, 56, OLED_6X8, "%s", status_line);
}

static void Menu_PageWater(KeyEvent_t key, uint8_t *exit_flag)
{
    char current_text[16];
    char threshold_text[16];

    Menu_HandleBackKey(key, exit_flag);
    if (key != KEY_NONE)
    {
        return;
    }

    snprintf(current_text, sizeof(current_text), "%u%%", (unsigned)g_menu_state.water_percent);
    snprintf(threshold_text, sizeof(threshold_text), "TH %ld%%", (long)g_menu_state.water_threshold_percent);

    Menu_DrawCenteredText(0, OLED_6X8, "Water");
    Menu_DrawCenteredText(10, OLED_8X16, current_text);
    Menu_DrawCenteredText(28, OLED_8X16, threshold_text);
    Menu_PrintAt(0, 48, OLED_6X8, "State:%s",
                 Menu_IsAlarmActive(MENU_ALARM_WATER) ? "ALARM" : "NORM");
    Menu_PrintAt(0, 56, OLED_6X8, "ADC:%4u B:Back", (unsigned)g_menu_state.water_adc_raw);
}

static void Menu_PageHuman(KeyEvent_t key, uint8_t *exit_flag)
{
    char raw_text[16];

    Menu_HandleBackKey(key, exit_flag);
    if (key != KEY_NONE)
    {
        return;
    }

    snprintf(raw_text, sizeof(raw_text), "RAW %s",
             (g_menu_state.human_level != 0U) ? "HIGH" : "LOW");

    Menu_DrawCenteredText(0, OLED_6X8, "Human");
    Menu_DrawCenteredText(10, OLED_8X16,
                          Menu_IsAlarmActive(MENU_ALARM_HUMAN) ? "ALARM" : "SAFE");
    Menu_DrawCenteredText(28, OLED_8X16, raw_text);
    Menu_PrintAt(0, 48, OLED_6X8, "Lv:%u Ev:%s",
                 (unsigned)g_menu_state.human_level,
                 (g_menu_state.human_alarm_active != 0U) ? "TRIG" : "IDLE");
    Menu_DrawCenteredText(56, OLED_6X8, "B:Back");
}

static void Menu_PageFlame(KeyEvent_t key, uint8_t *exit_flag)
{
    char ao_text[16];

    Menu_HandleBackKey(key, exit_flag);
    if (key != KEY_NONE)
    {
        return;
    }

    snprintf(ao_text, sizeof(ao_text), "AO %u%%", (unsigned)g_menu_state.flame_ao_percent);

    Menu_DrawCenteredText(0, OLED_6X8, "Flame");
    Menu_DrawCenteredText(10, OLED_8X16,
                          Menu_IsAlarmActive(MENU_ALARM_FLAME) ? "FIRE" : "SAFE");
    Menu_DrawCenteredText(28, OLED_8X16, ao_text);
    Menu_PrintAt(0, 48, OLED_6X8, "ADC:%4u DO:%u",
                 (unsigned)g_menu_state.flame_adc_raw,
                 (unsigned)g_menu_state.flame_do);
    Menu_DrawCenteredText(56, OLED_6X8, "B:Back");
}

static void Menu_PageAlarmInfo(KeyEvent_t key, uint8_t *exit_flag)
{
    uint8_t alarm_count;
    uint8_t max_start;
    uint8_t line_index;

    alarm_count = Menu_GetAlarmCount();
    max_start = 0U;
    if (alarm_count > MENU_ALARM_PAGE_LINES)
    {
        max_start = (uint8_t)(((alarm_count - 1U) / MENU_ALARM_PAGE_LINES) * MENU_ALARM_PAGE_LINES);
    }

    if (key == KEY_BACK)
    {
        if (exit_flag != NULL)
        {
            *exit_flag = 1U;
        }
        return;
    }

    if ((alarm_count > MENU_ALARM_PAGE_LINES) && (key == KEY_UP))
    {
        if (g_menu_state.alarm_page_scroll >= MENU_ALARM_PAGE_LINES)
        {
            g_menu_state.alarm_page_scroll = (uint8_t)(g_menu_state.alarm_page_scroll - MENU_ALARM_PAGE_LINES);
        }
        else
        {
            g_menu_state.alarm_page_scroll = 0U;
        }
        return;
    }

    if ((alarm_count > MENU_ALARM_PAGE_LINES) && (key == KEY_DOWN))
    {
        if (g_menu_state.alarm_page_scroll + MENU_ALARM_PAGE_LINES < max_start)
        {
            g_menu_state.alarm_page_scroll = (uint8_t)(g_menu_state.alarm_page_scroll + MENU_ALARM_PAGE_LINES);
        }
        else
        {
            g_menu_state.alarm_page_scroll = max_start;
        }
        return;
    }

    if (key != KEY_NONE)
    {
        return;
    }

    Menu_ClampAlarmScroll();
    Menu_DrawCenteredText(0, OLED_6X8, "Alarm Info");

    if (alarm_count == 0U)
    {
        Menu_DrawCenteredText(18, OLED_8X16, "NORMAL");
        Menu_DrawCenteredText(40, OLED_6X8, "No active alarm");
        Menu_DrawCenteredText(56, OLED_6X8, "B:Back");
        return;
    }

    Menu_PrintAt(0, 8, OLED_6X8, "ALARM x%u", (unsigned)alarm_count);

    for (line_index = 0U; line_index < MENU_ALARM_PAGE_LINES; ++line_index)
    {
        uint8_t alarm_index;

        alarm_index = (uint8_t)(g_menu_state.alarm_page_scroll + line_index);
        if (alarm_index >= alarm_count)
        {
            break;
        }

        OLED_ShowString(0, (uint8_t)(16U + line_index * 8U),
                        (char *)Menu_GetActiveAlarmText(alarm_index), OLED_6X8);
    }

    if (alarm_count > MENU_ALARM_PAGE_LINES)
    {
        Menu_DrawCenteredText(56, OLED_6X8, "B:Back U/D:Pg");
    }
    else
    {
        Menu_DrawCenteredText(56, OLED_6X8, "B:Back");
    }
}

void Menu_Init(void)
{
    uint32_t now;

    Menu_LoadDefaults();

    OLED_Init();
    Key_Init();
    Buzzer_Init();
    MQ2_Smoke_Init();
    Water_Sensor_Init();
    CDCM_Init();
    Menu_CalibrateCurrentZero();
    SR505_Init();
    Flame_Init();

    g_menu_state.dht_status = DHT11_Init();

    Menu_BuildMenus();
    Menu_SampleFastSensors();
    Menu_SampleDhtSensor();
    Menu_UpdateAlarmState();

    now = HAL_GetTick();
    g_menu_state.last_fast_sample_ms = now;
    g_menu_state.last_dht_sample_ms = now;
    g_menu_state.last_ui_frame_ms = now;
    g_menu_state.ui_dirty = 1U;
}

void Menu_Task(void)
{
    uint32_t now;
    KeyEvent_t key;

    now = HAL_GetTick();
    key = Key_Scan();

    if (key != KEY_NONE)
    {
        MF_Process(key);
        g_menu_state.ui_dirty = 1U;
    }

    if ((now - g_menu_state.last_fast_sample_ms) >= MENU_FAST_SAMPLE_PERIOD_MS)
    {
        g_menu_state.last_fast_sample_ms = now;
        Menu_SampleFastSensors();
        Menu_UpdateAlarmState();
        g_menu_state.ui_dirty = 1U;
    }

    if ((now - g_menu_state.last_dht_sample_ms) >= MENU_DHT_SAMPLE_PERIOD_MS)
    {
        g_menu_state.last_dht_sample_ms = now;
        Menu_SampleDhtSensor();
        Menu_UpdateAlarmState();
        g_menu_state.ui_dirty = 1U;
    }

    if ((g_menu_state.ui_dirty != 0U) ||
        ((now - g_menu_state.last_ui_frame_ms) >= MENU_UI_FRAME_PERIOD_MS))
    {
        g_menu_state.last_ui_frame_ms = now;
        MF_Render();
        g_menu_state.ui_dirty = 0U;
    }
}
