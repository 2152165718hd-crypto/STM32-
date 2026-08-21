#include "main.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define MENU_SCREEN_W 128U
#define MENU_SCREEN_H 160U
#define MENU_BG ST7735_BLACK
#define MENU_TEXT ST7735_WHITE
#define MENU_OK ST7735_GREEN
#define MENU_WARN ST7735_YELLOW
#define MENU_ALARM ST7735_RED
#define MENU_SELECT ST7735_CYAN

#define MENU_HR_SAMPLE_COUNT 400U
#define MENU_HR_MIN_VALID_SAMPLES 300U
#define MENU_HR_SAMPLE_INTERVAL_MS 10U
#define MENU_HR_CALC_INTERVAL_MS 1000U
#define MENU_HR_RETRY_INTERVAL_MS 2000U
#define MENU_TEMP_INTERVAL_MS 500U
#define MENU_RENDER_INTERVAL_MS 300U
#define MENU_ALARM_INTERVAL_MS 500U

#define MENU_FINGER_IR_MIN 30000UL
#define MENU_MIN_IR_AC 500UL
#define MENU_MIN_RED_AC 300UL
#define MENU_PEAK_MIN_DISTANCE_SAMPLES 30U
#define MENU_PEAK_MAX_DISTANCE_SAMPLES 200U
#define MENU_PEAK_MAX_COUNT 18U
#define MENU_BODY_PRESENT_CONFIRM_COUNT 2U
#define MENU_BODY_ABSENT_CONFIRM_COUNT 1U

#define MENU_HR_MIN 30
#define MENU_HR_MAX 220
#define MENU_SPO2_MIN 70
#define MENU_SPO2_MAX 100
#define MENU_TEMP_MIN_TENTHS 300
#define MENU_TEMP_MAX_TENTHS 450

typedef enum
{
    MENU_SCREEN_MONITOR = 0,
    MENU_SCREEN_SETTINGS,
    MENU_SCREEN_EDIT
} MenuScreen_t;

typedef enum
{
    MENU_SET_HR_LOW = 0,
    MENU_SET_HR_HIGH,
    MENU_SET_SPO2_LOW,
    MENU_SET_SPO2_HIGH,
    MENU_SET_TEMP_LOW,
    MENU_SET_TEMP_HIGH,
    MENU_SET_COUNT
} MenuSettingItem_t;

typedef enum
{
    MENU_HR_STATUS_ERR = 0,
    MENU_HR_STATUS_NO_FINGER,
    MENU_HR_STATUS_WAIT,
    MENU_HR_STATUS_READY
} MenuHrStatus_t;

static uint32_t s_red_samples[MENU_HR_SAMPLE_COUNT];
static uint32_t s_ir_samples[MENU_HR_SAMPLE_COUNT];
static uint16_t s_sample_write = 0U;
static uint16_t s_sample_count = 0U;

static uint8_t s_hr_connected = 0U;
static MenuHrStatus_t s_hr_status = MENU_HR_STATUS_WAIT;
static uint8_t s_body_present = 0U;
static uint8_t s_body_present_count = 0U;
static uint8_t s_body_absent_count = 0U;
static uint8_t s_hr_valid = 0U;
static uint8_t s_spo2_valid = 0U;
static uint8_t s_temp_valid = 0U;
static int16_t s_hr_bpm = 0;
static int16_t s_spo2_percent = 0;
static int16_t s_temp_tenths = 0;

static int16_t s_hr_low = 60;
static int16_t s_hr_high = 100;
static int16_t s_spo2_low = 95;
static int16_t s_spo2_high = 100;
static int16_t s_temp_low = 360;
static int16_t s_temp_high = 375;

static uint8_t s_hr_alarm_low = 0U;
static uint8_t s_hr_alarm_high = 0U;
static uint8_t s_spo2_alarm_low = 0U;
static uint8_t s_spo2_alarm_high = 0U;
static uint8_t s_temp_alarm_low = 0U;
static uint8_t s_temp_alarm_high = 0U;
static uint8_t s_any_alarm = 0U;
static uint8_t s_alarm_phase = 0U;

static MenuScreen_t s_screen = MENU_SCREEN_MONITOR;
static MenuSettingItem_t s_selected_item = MENU_SET_HR_LOW;
static MenuSettingItem_t s_edit_item = MENU_SET_HR_LOW;
static int16_t s_edit_value = 0;

static uint8_t s_force_render = 1U;
static uint32_t s_last_sample_ms = 0U;
static uint32_t s_last_calc_ms = 0U;
static uint32_t s_last_temp_ms = 0U;
static uint32_t s_last_render_ms = 0U;
static uint32_t s_last_alarm_ms = 0U;
static uint32_t s_last_hr_retry_ms = 0U;

static uint8_t Menu_IsDue(uint32_t now, uint32_t *last, uint32_t interval)
{
    if ((uint32_t)(now - *last) >= interval)
    {
        *last = now;
        return 1U;
    }

    return 0U;
}

static void Menu_ClearLine(uint16_t y, uint16_t h)
{
    ST7735_DrawRectangle(0U, y, MENU_SCREEN_W, h, MENU_BG);
}

static void Menu_DrawLine(uint16_t y, const char *text, uint16_t color)
{
    Menu_ClearLine(y, Font_7x10.height);
    ST7735_DrawString(0U, y, text, color, MENU_BG, &Font_7x10);
}

static int16_t Menu_ClampInt16(int16_t value, int16_t min_value, int16_t max_value)
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

static int16_t Menu_CelsiusToTenths(float value)
{
    if (value >= 0.0f)
    {
        return (int16_t)((value * 10.0f) + 0.5f);
    }

    return (int16_t)((value * 10.0f) - 0.5f);
}

static int16_t Menu_SmoothInt16(int16_t old_value, uint8_t old_valid, int16_t new_value)
{
    if (old_valid == 0U)
    {
        return new_value;
    }

    return (int16_t)(((int32_t)old_value * 3 + new_value + 2) / 4);
}

static void Menu_FormatTenths(char *buffer, size_t buffer_size, int16_t tenths)
{
    int16_t whole = (int16_t)(tenths / 10);
    int16_t frac = (int16_t)(tenths % 10);

    if (frac < 0)
    {
        frac = (int16_t)-frac;
    }

    (void)snprintf(buffer, buffer_size, "%d.%d", (int)whole, (int)frac);
}

static uint32_t Menu_GetRedSample(uint16_t offset)
{
    uint16_t index;

    if (s_sample_count < MENU_HR_SAMPLE_COUNT)
    {
        index = offset;
    }
    else
    {
        index = (uint16_t)((s_sample_write + offset) % MENU_HR_SAMPLE_COUNT);
    }

    return s_red_samples[index];
}

static uint32_t Menu_GetIrSample(uint16_t offset)
{
    uint16_t index;

    if (s_sample_count < MENU_HR_SAMPLE_COUNT)
    {
        index = offset;
    }
    else
    {
        index = (uint16_t)((s_sample_write + offset) % MENU_HR_SAMPLE_COUNT);
    }

    return s_ir_samples[index];
}

static void Menu_ClearHrWindow(void)
{
    s_sample_write = 0U;
    s_sample_count = 0U;
    s_hr_valid = 0U;
    s_spo2_valid = 0U;
    s_hr_bpm = 0;
    s_spo2_percent = 0;
}

static void Menu_AppendHrSample(uint32_t red, uint32_t ir)
{
    s_red_samples[s_sample_write] = red;
    s_ir_samples[s_sample_write] = ir;
    s_sample_write = (uint16_t)((s_sample_write + 1U) % MENU_HR_SAMPLE_COUNT);

    if (s_sample_count < MENU_HR_SAMPLE_COUNT)
    {
        s_sample_count++;
    }
}

static uint8_t Menu_UpdateBodyPresence(uint8_t detected)
{
    uint8_t old_present = s_body_present;

    if (detected != 0U)
    {
        s_body_absent_count = 0U;
        if (s_body_present_count < MENU_BODY_PRESENT_CONFIRM_COUNT)
        {
            s_body_present_count++;
        }
        if (s_body_present_count >= MENU_BODY_PRESENT_CONFIRM_COUNT)
        {
            s_body_present = 1U;
        }
    }
    else
    {
        s_body_present_count = 0U;
        if (s_body_absent_count < MENU_BODY_ABSENT_CONFIRM_COUNT)
        {
            s_body_absent_count++;
        }
        if (s_body_absent_count >= MENU_BODY_ABSENT_CONFIRM_COUNT)
        {
            s_body_present = 0U;
        }
    }

    if (old_present != s_body_present)
    {
        s_force_render = 1U;
        return 1U;
    }

    return 0U;
}

static void Menu_UpdateAlarmState(void)
{
    uint8_t old_alarm = s_any_alarm;

    s_hr_alarm_low = (uint8_t)((s_hr_valid != 0U) && (s_hr_bpm < s_hr_low));
    s_hr_alarm_high = (uint8_t)((s_hr_valid != 0U) && (s_hr_bpm > s_hr_high));
    s_spo2_alarm_low = (uint8_t)((s_spo2_valid != 0U) && (s_spo2_percent < s_spo2_low));
    s_spo2_alarm_high = (uint8_t)((s_spo2_valid != 0U) && (s_spo2_percent > s_spo2_high));
    s_temp_alarm_low = (uint8_t)((s_body_present != 0U) && (s_temp_valid != 0U) && (s_temp_tenths < s_temp_low));
    s_temp_alarm_high = (uint8_t)((s_body_present != 0U) && (s_temp_valid != 0U) && (s_temp_tenths > s_temp_high));

    s_any_alarm = (uint8_t)(s_hr_alarm_low || s_hr_alarm_high ||
                            s_spo2_alarm_low || s_spo2_alarm_high ||
                            s_temp_alarm_low || s_temp_alarm_high);

    if (s_any_alarm == 0U)
    {
        s_alarm_phase = 0U;
        Buzzer_Off();
    }
    else if (old_alarm == 0U)
    {
        s_alarm_phase = 1U;
        s_last_alarm_ms = HAL_GetTick();
        Buzzer_On();
    }

    if (old_alarm != s_any_alarm)
    {
        s_force_render = 1U;
    }
}

static void Menu_ReadHrSensor(uint32_t now)
{
    HR_SensorSample_t sample;

    if (s_hr_connected == 0U)
    {
        if (Menu_IsDue(now, &s_last_hr_retry_ms, MENU_HR_RETRY_INTERVAL_MS) != 0U)
        {
            s_hr_connected = HR_Sensor_IsConnected();
            if (s_hr_connected != 0U)
            {
                (void)HR_Sensor_ConfigDefault();
                Menu_ClearHrWindow();
                s_hr_status = MENU_HR_STATUS_WAIT;
            }
        }
        if (Menu_UpdateBodyPresence(0U) != 0U)
        {
            Menu_UpdateAlarmState();
        }
        return;
    }

    if ((HR_Sensor_ReadSample(&sample) != HAL_OK) ||
        (sample.red == HR_SENSOR_INVALID_RAW) ||
        (sample.ir == HR_SENSOR_INVALID_RAW))
    {
        s_hr_connected = 0U;
        s_hr_status = MENU_HR_STATUS_ERR;
        Menu_ClearHrWindow();
        (void)Menu_UpdateBodyPresence(0U);
        Menu_UpdateAlarmState();
        return;
    }

    if (sample.ir < MENU_FINGER_IR_MIN)
    {
        s_hr_status = MENU_HR_STATUS_NO_FINGER;
        Menu_ClearHrWindow();
        (void)Menu_UpdateBodyPresence(0U);
        Menu_UpdateAlarmState();
        return;
    }

    Menu_AppendHrSample(sample.red, sample.ir);
}

static void Menu_AnalyzeHrWindow(void)
{
    uint16_t i;
    uint64_t sum_ir = 0ULL;
    uint64_t sum_red = 0ULL;
    uint32_t min_ir = 0xFFFFFFFFUL;
    uint32_t max_ir = 0UL;
    uint32_t min_red = 0xFFFFFFFFUL;
    uint32_t max_red = 0UL;
    uint32_t mean_ir;
    uint32_t mean_red;
    uint32_t ir_ac;
    uint32_t red_ac;
    uint16_t peaks[MENU_PEAK_MAX_COUNT];
    uint32_t peak_values[MENU_PEAK_MAX_COUNT];
    uint16_t peak_count = 0U;
    uint32_t threshold;
    uint32_t interval_sum = 0UL;
    uint16_t interval_count = 0U;
    uint8_t new_hr_valid = 0U;
    uint8_t new_spo2_valid = 0U;
    int16_t new_hr = 0;
    int16_t new_spo2 = 0;

    if (s_hr_connected == 0U)
    {
        s_hr_status = MENU_HR_STATUS_ERR;
        s_hr_valid = 0U;
        s_spo2_valid = 0U;
        (void)Menu_UpdateBodyPresence(0U);
        Menu_UpdateAlarmState();
        return;
    }

    if (s_sample_count < MENU_HR_MIN_VALID_SAMPLES)
    {
        s_hr_status = MENU_HR_STATUS_WAIT;
        s_hr_valid = 0U;
        s_spo2_valid = 0U;
        (void)Menu_UpdateBodyPresence(0U);
        Menu_UpdateAlarmState();
        return;
    }

    for (i = 0U; i < s_sample_count; i++)
    {
        uint32_t ir = Menu_GetIrSample(i);
        uint32_t red = Menu_GetRedSample(i);

        sum_ir += ir;
        sum_red += red;

        if (ir < min_ir)
        {
            min_ir = ir;
        }
        if (ir > max_ir)
        {
            max_ir = ir;
        }
        if (red < min_red)
        {
            min_red = red;
        }
        if (red > max_red)
        {
            max_red = red;
        }
    }

    mean_ir = (uint32_t)(sum_ir / s_sample_count);
    mean_red = (uint32_t)(sum_red / s_sample_count);
    ir_ac = max_ir - min_ir;
    red_ac = max_red - min_red;

    if ((mean_ir < MENU_FINGER_IR_MIN) || (ir_ac < MENU_MIN_IR_AC))
    {
        s_hr_status = MENU_HR_STATUS_NO_FINGER;
        s_hr_valid = 0U;
        s_spo2_valid = 0U;
        (void)Menu_UpdateBodyPresence(0U);
        Menu_UpdateAlarmState();
        return;
    }

    (void)Menu_UpdateBodyPresence(1U);
    threshold = mean_ir + (ir_ac / 8UL);

    for (i = 1U; i < (uint16_t)(s_sample_count - 1U); i++)
    {
        uint32_t prev = Menu_GetIrSample((uint16_t)(i - 1U));
        uint32_t curr = Menu_GetIrSample(i);
        uint32_t next = Menu_GetIrSample((uint16_t)(i + 1U));

        if ((curr > prev) && (curr >= next) && (curr > threshold))
        {
            if ((peak_count > 0U) &&
                ((uint16_t)(i - peaks[peak_count - 1U]) < MENU_PEAK_MIN_DISTANCE_SAMPLES))
            {
                if (curr > peak_values[peak_count - 1U])
                {
                    peaks[peak_count - 1U] = i;
                    peak_values[peak_count - 1U] = curr;
                }
            }
            else if (peak_count < MENU_PEAK_MAX_COUNT)
            {
                peaks[peak_count] = i;
                peak_values[peak_count] = curr;
                peak_count++;
            }
        }
    }

    if (peak_count >= 3U)
    {
        for (i = 1U; i < peak_count; i++)
        {
            uint16_t interval = (uint16_t)(peaks[i] - peaks[i - 1U]);

            if ((interval >= MENU_PEAK_MIN_DISTANCE_SAMPLES) &&
                (interval <= MENU_PEAK_MAX_DISTANCE_SAMPLES))
            {
                interval_sum += interval;
                interval_count++;
            }
        }

        if (interval_count >= 2U)
        {
            uint32_t avg_interval = interval_sum / interval_count;
            int16_t hr = (int16_t)((6000UL + (avg_interval / 2UL)) / avg_interval);

            if ((hr >= MENU_HR_MIN) && (hr <= MENU_HR_MAX))
            {
                new_hr = hr;
                new_hr_valid = 1U;
            }
        }
    }

    if ((mean_red > 0UL) && (ir_ac >= MENU_MIN_IR_AC) && (red_ac >= MENU_MIN_RED_AC))
    {
        uint32_t ratio_x100 = (uint32_t)(((uint64_t)red_ac * mean_ir * 100ULL) /
                                         ((uint64_t)ir_ac * mean_red));

        if ((ratio_x100 >= 40UL) && (ratio_x100 <= 140UL))
        {
            int32_t spo2 = 110L - (int32_t)((25UL * ratio_x100 + 50UL) / 100UL);

            if (spo2 > MENU_SPO2_MAX)
            {
                spo2 = MENU_SPO2_MAX;
            }
            if (spo2 >= MENU_SPO2_MIN)
            {
                new_spo2 = (int16_t)spo2;
                new_spo2_valid = 1U;
            }
        }
    }

    if (new_hr_valid != 0U)
    {
        s_hr_bpm = Menu_SmoothInt16(s_hr_bpm, s_hr_valid, new_hr);
        s_hr_valid = 1U;
    }
    else
    {
        s_hr_valid = 0U;
    }

    if (new_spo2_valid != 0U)
    {
        s_spo2_percent = Menu_SmoothInt16(s_spo2_percent, s_spo2_valid, new_spo2);
        s_spo2_valid = 1U;
    }
    else
    {
        s_spo2_valid = 0U;
    }

    s_hr_status = ((s_hr_valid != 0U) || (s_spo2_valid != 0U)) ? MENU_HR_STATUS_READY : MENU_HR_STATUS_WAIT;
    Menu_UpdateAlarmState();
}

static void Menu_ReadTemperature(void)
{
    float temp_c = IR_Temp_ReadObjectCelsius();

    if ((temp_c <= (IR_TEMP_INVALID_CELSIUS + 0.5f)) || (temp_c < -40.0f) || (temp_c > 125.0f))
    {
        s_temp_valid = 0U;
        Menu_UpdateAlarmState();
        return;
    }

    s_temp_tenths = Menu_SmoothInt16(s_temp_tenths, s_temp_valid, Menu_CelsiusToTenths(temp_c));
    s_temp_valid = 1U;
    Menu_UpdateAlarmState();
}

static int16_t Menu_GetSettingValue(MenuSettingItem_t item)
{
    switch (item)
    {
    case MENU_SET_HR_LOW:
        return s_hr_low;
    case MENU_SET_HR_HIGH:
        return s_hr_high;
    case MENU_SET_SPO2_LOW:
        return s_spo2_low;
    case MENU_SET_SPO2_HIGH:
        return s_spo2_high;
    case MENU_SET_TEMP_LOW:
        return s_temp_low;
    case MENU_SET_TEMP_HIGH:
        return s_temp_high;
    default:
        return 0;
    }
}

static int16_t Menu_GetSettingMin(MenuSettingItem_t item)
{
    switch (item)
    {
    case MENU_SET_HR_LOW:
        return MENU_HR_MIN;
    case MENU_SET_HR_HIGH:
        return (int16_t)(s_hr_low + 1);
    case MENU_SET_SPO2_LOW:
        return MENU_SPO2_MIN;
    case MENU_SET_SPO2_HIGH:
        return (int16_t)(s_spo2_low + 1);
    case MENU_SET_TEMP_LOW:
        return MENU_TEMP_MIN_TENTHS;
    case MENU_SET_TEMP_HIGH:
        return (int16_t)(s_temp_low + 1);
    default:
        return 0;
    }
}

static int16_t Menu_GetSettingMax(MenuSettingItem_t item)
{
    switch (item)
    {
    case MENU_SET_HR_LOW:
        return (int16_t)(s_hr_high - 1);
    case MENU_SET_HR_HIGH:
        return MENU_HR_MAX;
    case MENU_SET_SPO2_LOW:
        return (int16_t)(s_spo2_high - 1);
    case MENU_SET_SPO2_HIGH:
        return MENU_SPO2_MAX;
    case MENU_SET_TEMP_LOW:
        return (int16_t)(s_temp_high - 1);
    case MENU_SET_TEMP_HIGH:
        return MENU_TEMP_MAX_TENTHS;
    default:
        return 0;
    }
}

static void Menu_SetSettingValue(MenuSettingItem_t item, int16_t value)
{
    value = Menu_ClampInt16(value, Menu_GetSettingMin(item), Menu_GetSettingMax(item));

    switch (item)
    {
    case MENU_SET_HR_LOW:
        s_hr_low = value;
        break;
    case MENU_SET_HR_HIGH:
        s_hr_high = value;
        break;
    case MENU_SET_SPO2_LOW:
        s_spo2_low = value;
        break;
    case MENU_SET_SPO2_HIGH:
        s_spo2_high = value;
        break;
    case MENU_SET_TEMP_LOW:
        s_temp_low = value;
        break;
    case MENU_SET_TEMP_HIGH:
        s_temp_high = value;
        break;
    default:
        break;
    }
}

static const char *Menu_GetSettingLabel(MenuSettingItem_t item)
{
    switch (item)
    {
    case MENU_SET_HR_LOW:
        return "HR LOW";
    case MENU_SET_HR_HIGH:
        return "HR HIGH";
    case MENU_SET_SPO2_LOW:
        return "O2 LOW";
    case MENU_SET_SPO2_HIGH:
        return "O2 HIGH";
    case MENU_SET_TEMP_LOW:
        return "TP LOW";
    case MENU_SET_TEMP_HIGH:
        return "TP HIGH";
    default:
        return "UNKNOWN";
    }
}

static void Menu_FormatSettingValue(MenuSettingItem_t item, int16_t value, char *buffer, size_t buffer_size)
{
    if ((item == MENU_SET_TEMP_LOW) || (item == MENU_SET_TEMP_HIGH))
    {
        Menu_FormatTenths(buffer, buffer_size, value);
    }
    else
    {
        (void)snprintf(buffer, buffer_size, "%d", (int)value);
    }
}

static void Menu_BuildAlarmText(char *buffer, size_t buffer_size)
{
    const char *msg = "ALM: NONE";
    uint8_t count = 0U;

    if (s_hr_alarm_low != 0U)
    {
        msg = "ALM: HR LOW";
        count++;
    }
    if (s_hr_alarm_high != 0U)
    {
        msg = "ALM: HR HIGH";
        count++;
    }
    if (s_spo2_alarm_low != 0U)
    {
        msg = (count == 0U) ? "ALM: O2 LOW" : msg;
        count++;
    }
    if (s_spo2_alarm_high != 0U)
    {
        msg = (count == 0U) ? "ALM: O2 HIGH" : msg;
        count++;
    }
    if (s_temp_alarm_low != 0U)
    {
        msg = (count == 0U) ? "ALM: TP LOW" : msg;
        count++;
    }
    if (s_temp_alarm_high != 0U)
    {
        msg = (count == 0U) ? "ALM: TP HIGH" : msg;
        count++;
    }

    if (count > 1U)
    {
        (void)snprintf(buffer, buffer_size, "%s +", msg);
    }
    else
    {
        (void)snprintf(buffer, buffer_size, "%s", msg);
    }
}

static uint16_t Menu_ValueColor(uint8_t valid, uint8_t alarm_low, uint8_t alarm_high)
{
    if (valid == 0U)
    {
        return MENU_WARN;
    }

    if ((alarm_low != 0U) || (alarm_high != 0U))
    {
        return (s_alarm_phase != 0U) ? MENU_ALARM : MENU_WARN;
    }

    return MENU_OK;
}

static void Menu_RenderMonitor(void)
{
    char line[32];
    char alarm[24];
    char temp_low[8];
    char temp_high[8];

    Menu_DrawLine(2U, "HEALTH MONITOR", MENU_SELECT);

    if (s_hr_valid != 0U)
    {
        (void)snprintf(line, sizeof(line), "HR:%3d bpm %s", (int)s_hr_bpm,
                       ((s_hr_alarm_low != 0U) || (s_hr_alarm_high != 0U)) ? "ALM" : "OK");
    }
    else
    {
        (void)snprintf(line, sizeof(line), "HR: -- bpm");
    }
    Menu_DrawLine(20U, line, Menu_ValueColor(s_hr_valid, s_hr_alarm_low, s_hr_alarm_high));

    if (s_spo2_valid != 0U)
    {
        (void)snprintf(line, sizeof(line), "SPO2:%3d%% %s", (int)s_spo2_percent,
                       ((s_spo2_alarm_low != 0U) || (s_spo2_alarm_high != 0U)) ? "ALM" : "OK");
    }
    else
    {
        (void)snprintf(line, sizeof(line), "SPO2: --%%");
    }
    Menu_DrawLine(38U, line, Menu_ValueColor(s_spo2_valid, s_spo2_alarm_low, s_spo2_alarm_high));

    if (s_body_present == 0U)
    {
        (void)snprintf(line, sizeof(line), "TEMP: NO BODY");
    }
    else if (s_temp_valid != 0U)
    {
        char temp[8];
        Menu_FormatTenths(temp, sizeof(temp), s_temp_tenths);
        (void)snprintf(line, sizeof(line), "TEMP:%s C %s", temp,
                       ((s_temp_alarm_low != 0U) || (s_temp_alarm_high != 0U)) ? "ALM" : "OK");
    }
    else
    {
        (void)snprintf(line, sizeof(line), "TEMP: ERR");
    }
    Menu_DrawLine(56U, line,
                  (s_body_present == 0U) ? MENU_WARN : Menu_ValueColor(s_temp_valid, s_temp_alarm_low, s_temp_alarm_high));

    if (s_hr_connected == 0U)
    {
        Menu_DrawLine(74U, "HR SENSOR ERR", MENU_WARN);
    }
    else if (s_hr_status == MENU_HR_STATUS_NO_FINGER)
    {
        Menu_DrawLine(74U, "NO FINGER", MENU_WARN);
    }
    else if (s_hr_status == MENU_HR_STATUS_WAIT)
    {
        Menu_DrawLine(74U, "WAIT SIGNAL", MENU_WARN);
    }
    else
    {
        Menu_DrawLine(74U, "SIGNAL OK", MENU_OK);
    }

    (void)snprintf(line, sizeof(line), "RNG HR %d-%d", (int)s_hr_low, (int)s_hr_high);
    Menu_DrawLine(90U, line, MENU_TEXT);

    (void)snprintf(line, sizeof(line), "RNG O2 %d-%d", (int)s_spo2_low, (int)s_spo2_high);
    Menu_DrawLine(104U, line, MENU_TEXT);

    Menu_FormatTenths(temp_low, sizeof(temp_low), s_temp_low);
    Menu_FormatTenths(temp_high, sizeof(temp_high), s_temp_high);
    (void)snprintf(line, sizeof(line), "RNG TP %s-%s", temp_low, temp_high);
    Menu_DrawLine(118U, line, MENU_TEXT);

    Menu_DrawLine(132U, "ENT:MENU", MENU_SELECT);
    Menu_BuildAlarmText(alarm, sizeof(alarm));
    Menu_DrawLine(146U, alarm, (s_any_alarm != 0U) ? Menu_ValueColor(1U, 1U, 0U) : MENU_OK);
}

static void Menu_RenderSettings(void)
{
    uint8_t i;
    char line[32];
    char value[8];

    Menu_DrawLine(2U, "SET ALARM RANGE", MENU_SELECT);

    for (i = 0U; i < MENU_SET_COUNT; i++)
    {
        MenuSettingItem_t item = (MenuSettingItem_t)i;
        char prefix = (item == s_selected_item) ? '>' : ' ';

        Menu_FormatSettingValue(item, Menu_GetSettingValue(item), value, sizeof(value));
        (void)snprintf(line, sizeof(line), "%c %-7s:%4s", prefix, Menu_GetSettingLabel(item), value);
        Menu_DrawLine((uint16_t)(20U + (i * 16U)), line,
                      (item == s_selected_item) ? MENU_SELECT : MENU_TEXT);
    }

    Menu_DrawLine(124U, "UP/DN MOVE", MENU_WARN);
    Menu_DrawLine(138U, "ENT EDIT", MENU_WARN);
    Menu_DrawLine(150U, "BACK EXIT", MENU_WARN);
}

static void Menu_RenderEdit(void)
{
    char line[32];
    char value[8];
    char min_value[8];
    char max_value[8];

    Menu_DrawLine(2U, "EDIT VALUE", MENU_SELECT);
    Menu_DrawLine(24U, Menu_GetSettingLabel(s_edit_item), MENU_TEXT);

    Menu_ClearLine(46U, Font_11x18.height);
    Menu_FormatSettingValue(s_edit_item, s_edit_value, value, sizeof(value));
    ST7735_DrawString(10U, 46U, value, MENU_SELECT, MENU_BG, &Font_11x18);

    Menu_FormatSettingValue(s_edit_item, Menu_GetSettingMin(s_edit_item), min_value, sizeof(min_value));
    Menu_FormatSettingValue(s_edit_item, Menu_GetSettingMax(s_edit_item), max_value, sizeof(max_value));
    (void)snprintf(line, sizeof(line), "RNG %s-%s", min_value, max_value);
    Menu_DrawLine(78U, line, MENU_TEXT);

    Menu_DrawLine(104U, "UP/DN CHANGE", MENU_WARN);
    Menu_DrawLine(122U, "ENT SAVE", MENU_WARN);
    Menu_DrawLine(140U, "BACK CANCEL", MENU_WARN);
}

static void Menu_Render(void)
{
    if (s_force_render != 0U)
    {
        ST7735_FillScreen(MENU_BG);
    }

    switch (s_screen)
    {
    case MENU_SCREEN_MONITOR:
        Menu_RenderMonitor();
        break;
    case MENU_SCREEN_SETTINGS:
        Menu_RenderSettings();
        break;
    case MENU_SCREEN_EDIT:
        Menu_RenderEdit();
        break;
    default:
        s_screen = MENU_SCREEN_MONITOR;
        Menu_RenderMonitor();
        break;
    }

    s_force_render = 0U;
}

static void Menu_OpenSettings(void)
{
    s_screen = MENU_SCREEN_SETTINGS;
    s_force_render = 1U;
}

static void Menu_OpenEdit(void)
{
    s_edit_item = s_selected_item;
    s_edit_value = Menu_GetSettingValue(s_edit_item);
    s_screen = MENU_SCREEN_EDIT;
    s_force_render = 1U;
}

static void Menu_SaveEdit(void)
{
    Menu_SetSettingValue(s_edit_item, s_edit_value);
    Menu_UpdateAlarmState();
    s_screen = MENU_SCREEN_SETTINGS;
    s_force_render = 1U;
}

static void Menu_AdjustEdit(int16_t delta)
{
    s_edit_value = Menu_ClampInt16((int16_t)(s_edit_value + delta),
                                   Menu_GetSettingMin(s_edit_item),
                                   Menu_GetSettingMax(s_edit_item));
    s_force_render = 1U;
}

static void Menu_HandleKey(KeyEvent_t key)
{
    if (key == KEY_NONE)
    {
        return;
    }

    switch (s_screen)
    {
    case MENU_SCREEN_MONITOR:
        if (key == KEY_ENTER)
        {
            Menu_OpenSettings();
        }
        break;

    case MENU_SCREEN_SETTINGS:
        if (key == KEY_UP)
        {
            if (s_selected_item == 0)
            {
                s_selected_item = (MenuSettingItem_t)(MENU_SET_COUNT - 1U);
            }
            else
            {
                s_selected_item = (MenuSettingItem_t)(s_selected_item - 1U);
            }
            s_force_render = 1U;
        }
        else if (key == KEY_DOWN)
        {
            s_selected_item = (MenuSettingItem_t)((s_selected_item + 1U) % MENU_SET_COUNT);
            s_force_render = 1U;
        }
        else if (key == KEY_ENTER)
        {
            Menu_OpenEdit();
        }
        else if (key == KEY_BACK)
        {
            s_screen = MENU_SCREEN_MONITOR;
            s_force_render = 1U;
        }
        break;

    case MENU_SCREEN_EDIT:
        if (key == KEY_UP)
        {
            Menu_AdjustEdit(1);
        }
        else if (key == KEY_DOWN)
        {
            Menu_AdjustEdit(-1);
        }
        else if (key == KEY_ENTER)
        {
            Menu_SaveEdit();
        }
        else if (key == KEY_BACK)
        {
            s_screen = MENU_SCREEN_SETTINGS;
            s_force_render = 1U;
        }
        break;

    default:
        s_screen = MENU_SCREEN_MONITOR;
        s_force_render = 1U;
        break;
    }
}

static void Menu_ProcessKeys(void)
{
    uint8_t guard = 0U;
    KeyEvent_t key;

    do
    {
        key = Key_Scan();
        Menu_HandleKey(key);
        guard++;
    } while ((key != KEY_NONE) && (guard < 8U));
}

static void Menu_ServiceAlarm(uint32_t now)
{
    if (s_any_alarm == 0U)
    {
        s_alarm_phase = 0U;
        Buzzer_Off();
        return;
    }

    if (Menu_IsDue(now, &s_last_alarm_ms, MENU_ALARM_INTERVAL_MS) != 0U)
    {
        s_alarm_phase = (uint8_t)!s_alarm_phase;
        if (s_alarm_phase != 0U)
        {
            Buzzer_On();
        }
        else
        {
            Buzzer_Off();
        }
        s_force_render = 1U;
    }
}

void Menu_Init(void)
{
    uint32_t now = HAL_GetTick();

    s_last_sample_ms = now;
    s_last_calc_ms = now;
    s_last_temp_ms = now;
    s_last_render_ms = now;
    s_last_alarm_ms = now;
    s_last_hr_retry_ms = now;

    s_screen = MENU_SCREEN_MONITOR;
    s_selected_item = MENU_SET_HR_LOW;
    s_hr_connected = HR_Sensor_IsConnected();
    s_hr_status = (s_hr_connected != 0U) ? MENU_HR_STATUS_WAIT : MENU_HR_STATUS_ERR;
    (void)Menu_UpdateBodyPresence(0U);

    Buzzer_Off();
    Menu_ReadTemperature();
    Menu_UpdateAlarmState();
    s_force_render = 1U;
    Menu_Render();
}

void Menu_Task(void)
{
    uint32_t now = HAL_GetTick();

    Menu_ProcessKeys();

    if (Menu_IsDue(now, &s_last_sample_ms, MENU_HR_SAMPLE_INTERVAL_MS) != 0U)
    {
        Menu_ReadHrSensor(now);
    }

    if (Menu_IsDue(now, &s_last_calc_ms, MENU_HR_CALC_INTERVAL_MS) != 0U)
    {
        Menu_AnalyzeHrWindow();
    }

    if (Menu_IsDue(now, &s_last_temp_ms, MENU_TEMP_INTERVAL_MS) != 0U)
    {
        Menu_ReadTemperature();
    }

    Menu_ServiceAlarm(now);

    if ((s_force_render != 0U) ||
        (Menu_IsDue(now, &s_last_render_ms, MENU_RENDER_INTERVAL_MS) != 0U))
    {
        s_last_render_ms = now;
        Menu_Render();
    }
}
