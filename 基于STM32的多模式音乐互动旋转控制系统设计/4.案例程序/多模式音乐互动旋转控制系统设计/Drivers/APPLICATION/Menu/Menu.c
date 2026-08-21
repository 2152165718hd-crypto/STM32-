#include "APPLICATION/Menu/Menu.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "APPLICATION/Menu/MenuFramework.h"
#include "Hardware/Bluetooth/Bluetooth.h"
#include "Hardware/Buzzer/Buzzer.h"
#include "Hardware/KEY/KEY.h"
#include "Hardware/LED/LED.h"
#include "Hardware/MAX9814/MAX9814.h"
#include "Hardware/Motor/Motor.h"
#include "Hardware/OLED/OLED.h"

#define APP_DUTY_MAX 1000u
#define APP_DEFAULT_SAFETY_THRESHOLD 900
#define APP_DEFAULT_MANUAL_SPEED 300
#define APP_MUSIC_NOISE_FLOOR 35u
#define APP_MUSIC_LOW_CUT 25u
#define APP_CONTROL_PERIOD_MS 20u
#define APP_DISPLAY_PERIOD_MS 100u
#define APP_ALARM_SAMPLE_PERIOD_MS 100u
#define APP_ALARM_WINDOW_SIZE 100u
#define APP_ALARM_TRIGGER_COUNT 50u
#define APP_ALARM_SUPPRESS_MS 2000u
#define APP_ALARM_BLINK_PERIOD_MS 250u
#define APP_BT_LINE_BUF_SIZE 48u
#define APP_BT_REPLY_BUF_SIZE 128u
#define APP_BT_IDLE_TIMEOUT_MS 300u
#define APP_AUTO_PERIOD_MS 8000u
#define APP_AUTO_HALF_PERIOD_MS (APP_AUTO_PERIOD_MS / 2u)
#define APP_AUTO_MIN_DUTY 150u
#define APP_AUTO_MAX_DUTY 850u

static MenuApp_Mode_t s_mode = MENU_APP_MODE_MUSIC;
static int32_t s_manual_speed_permille = APP_DEFAULT_MANUAL_SPEED;
static int32_t s_safety_threshold_permille = APP_DEFAULT_SAFETY_THRESHOLD;
static uint16_t s_target_duty_permille = 0u;
static uint16_t s_applied_duty_permille = 0u;
static uint16_t s_last_sound_level = 0u;
static uint16_t s_last_raw_sound = 0u;

static uint8_t s_alarm_active = 0u;
static uint8_t s_alarm_window[APP_ALARM_WINDOW_SIZE];
static uint8_t s_alarm_index = 0u;
static uint8_t s_alarm_count = 0u;
static uint8_t s_alarm_over_count = 0u;
static uint8_t s_alarm_output_on = 0u;
static uint32_t s_alarm_suppress_until = 0u;

static char s_bt_line[APP_BT_LINE_BUF_SIZE];
static uint8_t s_bt_line_len = 0u;
static uint32_t s_bt_last_rx_tick = 0u;

static uint32_t s_last_control_tick = 0u;
static uint32_t s_last_display_tick = 0u;
static uint32_t s_last_alarm_sample_tick = 0u;
static uint32_t s_last_alarm_blink_tick = 0u;

static MF_Menu_t *s_root_menu = NULL;
static MF_Menu_t *s_mode_menu = NULL;

static uint16_t MenuApp_ClampPermilleInt(int32_t value)
{
    if (value < 0)
    {
        return 0u;
    }
    if (value > (int32_t)APP_DUTY_MAX)
    {
        return APP_DUTY_MAX;
    }
    return (uint16_t)value;
}

static const char *MenuApp_SkipSpaces(const char *text)
{
    while ((*text == ' ') || (*text == '\t'))
    {
        text++;
    }
    return text;
}

static char *MenuApp_TrimLine(char *text)
{
    char *start = text;
    char *end;

    while ((*start == ' ') || (*start == '\t') || (*start == '\r') || (*start == '\n'))
    {
        start++;
    }

    end = start + strlen(start);
    while (end > start)
    {
        char ch = end[-1];
        if ((ch != ' ') && (ch != '\t') && (ch != '\r') && (ch != '\n'))
        {
            break;
        }
        end--;
    }

    *end = '\0';
    return start;
}

static uint8_t MenuApp_ParsePermille(const char *text, uint16_t *value)
{
    uint32_t acc = 0u;
    uint8_t has_digit = 0u;

    text = MenuApp_SkipSpaces(text);
    while ((*text >= '0') && (*text <= '9'))
    {
        has_digit = 1u;
        acc = (acc * 10u) + (uint32_t)(*text - '0');
        if (acc > APP_DUTY_MAX)
        {
            return 0u;
        }
        text++;
    }

    text = MenuApp_SkipSpaces(text);
    if ((has_digit == 0u) || (*text != '\0'))
    {
        return 0u;
    }

    *value = (uint16_t)acc;
    return 1u;
}

static const char *MenuApp_ModeName(MenuApp_Mode_t mode)
{
    switch (mode)
    {
    case MENU_APP_MODE_MUSIC:
        return "MUSIC";
    case MENU_APP_MODE_MANUAL:
        return "MANUAL";
    case MENU_APP_MODE_AUTO:
        return "AUTO";
    default:
        return "UNKNOWN";
    }
}

static const char *MenuApp_ModeNameShort(MenuApp_Mode_t mode)
{
    switch (mode)
    {
    case MENU_APP_MODE_MUSIC:
        return "Music";
    case MENU_APP_MODE_MANUAL:
        return "Manual";
    case MENU_APP_MODE_AUTO:
        return "Auto";
    default:
        return "Unknown";
    }
}

static void MenuApp_ResetAlarmWindow(void)
{
    memset(s_alarm_window, 0, sizeof(s_alarm_window));
    s_alarm_index = 0u;
    s_alarm_count = 0u;
    s_alarm_over_count = 0u;
}

static void MenuApp_SetAlarmOutputs(uint8_t on)
{
    s_alarm_output_on = on ? 1u : 0u;
    LED_Set(LED_GPIO_PORT, LED_GPIO_PIN, s_alarm_output_on);
    if (s_alarm_output_on != 0u)
    {
        Buzzer_On();
    }
    else
    {
        Buzzer_Off();
    }
}

static void MenuApp_ClearAlarm(void)
{
    s_alarm_active = 0u;
    s_alarm_suppress_until = HAL_GetTick() + APP_ALARM_SUPPRESS_MS;
    MenuApp_ResetAlarmWindow();
    MenuApp_SetAlarmOutputs(0u);
}

static void MenuApp_SetMode(MenuApp_Mode_t mode)
{
    s_mode = mode;
}

static void MenuApp_SelectMusic(void)
{
    MenuApp_SetMode(MENU_APP_MODE_MUSIC);
}

static void MenuApp_SelectManual(void)
{
    MenuApp_SetMode(MENU_APP_MODE_MANUAL);
}

static void MenuApp_SelectAuto(void)
{
    MenuApp_SetMode(MENU_APP_MODE_AUTO);
}

static void MenuApp_OnManualSpeedChanged(int32_t new_value)
{
    s_manual_speed_permille = (int32_t)MenuApp_ClampPermilleInt(new_value);
    MenuApp_SetMode(MENU_APP_MODE_MANUAL);
}

static void MenuApp_OnSafetyThresholdChanged(int32_t new_value)
{
    s_safety_threshold_permille = (int32_t)MenuApp_ClampPermilleInt(new_value);
    MenuApp_ResetAlarmWindow();
}

static uint16_t MenuApp_ComputeMusicTarget(uint16_t level)
{
    uint32_t normalized;
    uint32_t shaped;

    if (level <= APP_MUSIC_NOISE_FLOOR)
    {
        return 0u;
    }

    normalized = ((uint32_t)(level - APP_MUSIC_NOISE_FLOOR) * APP_DUTY_MAX) /
                 (APP_DUTY_MAX - APP_MUSIC_NOISE_FLOOR);
    if (normalized > APP_DUTY_MAX)
    {
        normalized = APP_DUTY_MAX;
    }

    shaped = (normalized * (650u + normalized)) / 1650u;
    if (shaped < APP_MUSIC_LOW_CUT)
    {
        shaped = 0u;
    }
    if (shaped > APP_DUTY_MAX)
    {
        shaped = APP_DUTY_MAX;
    }

    return (uint16_t)shaped;
}

static uint16_t MenuApp_ComputeAutoTarget(uint32_t now)
{
    uint32_t phase = now % APP_AUTO_PERIOD_MS;
    uint32_t span = APP_AUTO_MAX_DUTY - APP_AUTO_MIN_DUTY;
    uint32_t duty;

    if (phase < APP_AUTO_HALF_PERIOD_MS)
    {
        duty = APP_AUTO_MIN_DUTY + ((phase * span) / APP_AUTO_HALF_PERIOD_MS);
    }
    else
    {
        duty = APP_AUTO_MAX_DUTY -
               (((phase - APP_AUTO_HALF_PERIOD_MS) * span) / APP_AUTO_HALF_PERIOD_MS);
    }

    if (duty > APP_DUTY_MAX)
    {
        duty = APP_DUTY_MAX;
    }
    return (uint16_t)duty;
}

static uint16_t MenuApp_SlewDuty(uint16_t current, uint16_t target)
{
    uint16_t delta;
    uint16_t step;

    if (target > current)
    {
        delta = (uint16_t)(target - current);
        step = (uint16_t)((delta + 3u) / 4u);
        if (step > 80u)
        {
            step = 80u;
        }
        if (step == 0u)
        {
            step = 1u;
        }
        return (uint16_t)(current + step);
    }

    if (target < current)
    {
        delta = (uint16_t)(current - target);
        step = (uint16_t)((delta + 7u) / 8u);
        if (step > 35u)
        {
            step = 35u;
        }
        if (step == 0u)
        {
            step = 1u;
        }
        return (uint16_t)(current - step);
    }

    return current;
}

static void MenuApp_RunControlStep(uint32_t now)
{
    MAX9814_Task();
    s_last_sound_level = MAX9814_ReadMusicLevelPermille();
    s_last_raw_sound = MAX9814_ReadRaw();

    switch (s_mode)
    {
    case MENU_APP_MODE_MUSIC:
        s_target_duty_permille = MenuApp_ComputeMusicTarget(s_last_sound_level);
        break;
    case MENU_APP_MODE_MANUAL:
        s_target_duty_permille = MenuApp_ClampPermilleInt(s_manual_speed_permille);
        break;
    case MENU_APP_MODE_AUTO:
        s_target_duty_permille = MenuApp_ComputeAutoTarget(now);
        break;
    default:
        s_target_duty_permille = 0u;
        break;
    }

    s_applied_duty_permille = MenuApp_SlewDuty(s_applied_duty_permille, s_target_duty_permille);
    Motor_SetDutyPermille(s_applied_duty_permille);
}

static void MenuApp_UpdateControl(uint32_t now)
{
    if ((uint32_t)(now - s_last_control_tick) < APP_CONTROL_PERIOD_MS)
    {
        return;
    }
    s_last_control_tick = now;

    MenuApp_RunControlStep(now);
}

static void MenuApp_RefreshControlImmediate(void)
{
    uint32_t now = HAL_GetTick();

    s_last_control_tick = now;
    MenuApp_RunControlStep(now);
}

static void MenuApp_PushAlarmSample(uint8_t over_threshold)
{
    if (s_alarm_count < APP_ALARM_WINDOW_SIZE)
    {
        s_alarm_count++;
    }
    else if (s_alarm_window[s_alarm_index] != 0u)
    {
        s_alarm_over_count--;
    }

    s_alarm_window[s_alarm_index] = over_threshold ? 1u : 0u;
    if (over_threshold != 0u)
    {
        s_alarm_over_count++;
    }

    s_alarm_index++;
    if (s_alarm_index >= APP_ALARM_WINDOW_SIZE)
    {
        s_alarm_index = 0u;
    }
}

static void MenuApp_UpdateAlarm(uint32_t now)
{
    uint8_t over_threshold;

    if ((uint32_t)(now - s_last_alarm_sample_tick) < APP_ALARM_SAMPLE_PERIOD_MS)
    {
        return;
    }
    s_last_alarm_sample_tick = now;

    over_threshold = (s_applied_duty_permille >
                      (uint16_t)MenuApp_ClampPermilleInt(s_safety_threshold_permille))
                         ? 1u
                         : 0u;
    MenuApp_PushAlarmSample(over_threshold);

    if ((s_alarm_active == 0u) &&
        ((int32_t)(now - s_alarm_suppress_until) >= 0) &&
        (s_alarm_over_count >= APP_ALARM_TRIGGER_COUNT))
    {
        s_alarm_active = 1u;
        s_last_alarm_blink_tick = 0u;
        MenuApp_SetAlarmOutputs(1u);
    }
}

static void MenuApp_UpdateAlarmOutput(uint32_t now)
{
    if (s_alarm_active == 0u)
    {
        if (s_alarm_output_on != 0u)
        {
            MenuApp_SetAlarmOutputs(0u);
        }
        return;
    }

    if ((uint32_t)(now - s_last_alarm_blink_tick) >= APP_ALARM_BLINK_PERIOD_MS)
    {
        s_last_alarm_blink_tick = now;
        MenuApp_SetAlarmOutputs((s_alarm_output_on == 0u) ? 1u : 0u);
    }
}

static void MenuApp_SendStatus(void)
{
    char buf[APP_BT_REPLY_BUF_SIZE];
    int len;

    len = snprintf(buf,
                   sizeof(buf),
                   "STAT MODE=%s SPD=%ld DUTY=%u THR=%ld ALM=%u SND=%u RAW=%u BT=%u\r\n",
                   MenuApp_ModeName(s_mode),
                   (long)s_manual_speed_permille,
                   (unsigned int)s_applied_duty_permille,
                   (long)s_safety_threshold_permille,
                   (unsigned int)s_alarm_active,
                   (unsigned int)s_last_sound_level,
                   (unsigned int)s_last_raw_sound,
                   (unsigned int)Bluetooth_IsConnected());

    if (len > 0)
    {
        if ((uint32_t)len >= sizeof(buf))
        {
            len = (int)sizeof(buf) - 1;
        }
        Bluetooth_Send((const uint8_t *)buf, (uint16_t)len);
    }
}

static void MenuApp_SendReplyf(const char *format, ...)
{
    char buf[APP_BT_REPLY_BUF_SIZE];
    va_list args;
    int len;

    va_start(args, format);
    len = vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    if (len <= 0)
    {
        return;
    }
    if ((uint32_t)len >= sizeof(buf))
    {
        len = (int)sizeof(buf) - 1;
    }

    Bluetooth_Send((const uint8_t *)buf, (uint16_t)len);
}

static void MenuApp_SendError(const char *reason)
{
    MenuApp_SendReplyf("%s\r\n", reason);
}

static void MenuApp_SendOkValue(const char *key, uint16_t value)
{
    MenuApp_SendReplyf("%s=%u OK\r\n", key, (unsigned int)value);
}

static void MenuApp_SendOkText(const char *key, const char *value)
{
    MenuApp_SendReplyf("%s=%s OK\r\n", key, value);
}

static uint8_t MenuApp_LineHasInlineWhitespace(const char *text)
{
    while (*text != '\0')
    {
        if ((*text == ' ') || (*text == '\t'))
        {
            return 1u;
        }
        text++;
    }

    return 0u;
}

static uint8_t MenuApp_ParseModeValue(const char *text, MenuApp_Mode_t *mode)
{
    if (strcmp(text, "MUSIC") == 0)
    {
        *mode = MENU_APP_MODE_MUSIC;
        return 1u;
    }
    if (strcmp(text, "MANUAL") == 0)
    {
        *mode = MENU_APP_MODE_MANUAL;
        return 1u;
    }
    if (strcmp(text, "AUTO") == 0)
    {
        *mode = MENU_APP_MODE_AUTO;
        return 1u;
    }

    return 0u;
}

static uint8_t MenuApp_SplitBluetoothCommand(char *line, char **key, char **value)
{
    char *separator = strchr(line, '=');

    if ((separator == NULL) || (strchr(separator + 1, '=') != NULL))
    {
        return 0u;
    }

    *separator = '\0';
    *key = line;
    *value = separator + 1;

    if (((*key)[0] == '\0') || ((*value)[0] == '\0'))
    {
        return 0u;
    }

    return 1u;
}

static void MenuApp_HandleBluetoothLine(char *line)
{
    char *key;
    char *value;
    uint16_t permille;
    MenuApp_Mode_t next_mode;

    line = MenuApp_TrimLine(line);

    if (*line == '\0')
    {
        return;
    }

    if ((strchr(line, ',') != NULL) || (strchr(line, ';') != NULL) || (MenuApp_LineHasInlineWhitespace(line) != 0u))
    {
        MenuApp_SendError("ERR FORMAT");
        return;
    }

    if (strcmp(line, "STAT?") == 0)
    {
        MenuApp_SendStatus();
        return;
    }

    if (MenuApp_SplitBluetoothCommand(line, &key, &value) == 0u)
    {
        MenuApp_SendError("ERR FORMAT");
        return;
    }

    if (strcmp(key, "SPD") == 0)
    {
        if (MenuApp_ParsePermille(value, &permille) == 0u)
        {
            MenuApp_SendError("ERR VALUE");
            return;
        }

        s_manual_speed_permille = (int32_t)permille;
        s_mode = MENU_APP_MODE_MANUAL;
        MenuApp_RefreshControlImmediate();
        MenuApp_SendOkValue("SPD", permille);
        return;
    }

    if (strcmp(key, "MODE") == 0)
    {
        if (MenuApp_ParseModeValue(value, &next_mode) == 0u)
        {
            MenuApp_SendError("ERR VALUE");
            return;
        }

        s_mode = next_mode;
        MenuApp_RefreshControlImmediate();
        MenuApp_SendOkText("MODE", MenuApp_ModeName(next_mode));
        return;
    }

    if (strcmp(key, "THR") == 0)
    {
        if (MenuApp_ParsePermille(value, &permille) == 0u)
        {
            MenuApp_SendError("ERR VALUE");
            return;
        }

        s_safety_threshold_permille = (int32_t)permille;
        MenuApp_ResetAlarmWindow();
        MenuApp_SendOkValue("THR", permille);
        return;
    }

    if (strcmp(key, "CLR") == 0)
    {
        if (strcmp(value, "ALARM") != 0)
        {
            MenuApp_SendError("ERR VALUE");
            return;
        }

        MenuApp_ClearAlarm();
        MenuApp_SendOkText("CLR", "ALARM");
        return;
    }

    MenuApp_SendError("ERR CMD");
}

static void MenuApp_ServiceBluetooth(uint32_t now)
{
    uint8_t byte_value;
    uint8_t received_any = 0u;

    while (Bluetooth_ReadByte(&byte_value) != 0u)
    {
        received_any = 1u;
        s_bt_last_rx_tick = now;

        char ch = (char)byte_value;

        if ((ch == '\r') || (ch == '\n'))
        {
            if (s_bt_line_len > 0u)
            {
                s_bt_line[s_bt_line_len] = '\0';
                MenuApp_HandleBluetoothLine(s_bt_line);
                s_bt_line_len = 0u;
            }
        }
        else if (s_bt_line_len < (APP_BT_LINE_BUF_SIZE - 1u))
        {
            s_bt_line[s_bt_line_len++] = ch;
        }
        else
        {
            s_bt_line_len = 0u;
            MenuApp_SendError("ERR FORMAT");
        }
    }

    if ((received_any == 0u) &&
        (s_bt_line_len > 0u) &&
        ((uint32_t)(now - s_bt_last_rx_tick) >= APP_BT_IDLE_TIMEOUT_MS))
    {
        s_bt_line[s_bt_line_len] = '\0';
        MenuApp_HandleBluetoothLine(s_bt_line);
        s_bt_line_len = 0u;
        s_bt_last_rx_tick = now;
    }
}

static void MenuApp_DrawTextLine(uint8_t row, const char *text)
{
    OLED_ShowString(0, (int16_t)(row * 8u), (char *)text, OLED_6X8);
}

static void MenuApp_RenderInfoPage(KeyEvent_t key, uint8_t *exit_flag)
{
    char line[24];
    uint8_t bar_width;

    if (key == KEY_BACK)
    {
        *exit_flag = 1u;
        return;
    }

    snprintf(line, sizeof(line), "Info  B:Back");
    MenuApp_DrawTextLine(0u, line);

    snprintf(line, sizeof(line), "Mode:%s", MenuApp_ModeNameShort(s_mode));
    MenuApp_DrawTextLine(1u, line);

    snprintf(line,
             sizeof(line),
             "PCT:%03u Duty:%04u",
             (unsigned int)((s_applied_duty_permille + 5u) / 10u),
             (unsigned int)s_applied_duty_permille);
    MenuApp_DrawTextLine(2u, line);

    snprintf(line,
             sizeof(line),
             "Sound:%04u Raw:%04u",
             (unsigned int)s_last_sound_level,
             (unsigned int)s_last_raw_sound);
    MenuApp_DrawTextLine(3u, line);

    snprintf(line,
             sizeof(line),
             "Thr:%04ld Alm:%s",
             (long)s_safety_threshold_permille,
             (s_alarm_active != 0u) ? "ON" : "OFF");
    MenuApp_DrawTextLine(4u, line);

    snprintf(line,
             sizeof(line),
             "BT:%s Man:%04ld",
             (Bluetooth_IsConnected() != 0u) ? "ON" : "OFF",
             (long)s_manual_speed_permille);
    MenuApp_DrawTextLine(5u, line);

    snprintf(line, sizeof(line), "Target:%04u", (unsigned int)s_target_duty_permille);
    MenuApp_DrawTextLine(6u, line);

    OLED_DrawRectangle(0, 58, 128, 6, OLED_UNFILLED);
    bar_width = (uint8_t)(((uint32_t)s_applied_duty_permille * 126u) / APP_DUTY_MAX);
    if (bar_width > 0u)
    {
        OLED_DrawRectangle(1, 59, bar_width, 4, OLED_FILLED);
    }
}

static void MenuApp_BuildMenu(void)
{
    MF_Reset();

    s_root_menu = MF_CreateMenu("Main");
    s_mode_menu = MF_CreateMenu("Mode");

    MF_AddAction(s_mode_menu, "Music", MenuApp_SelectMusic);
    MF_AddAction(s_mode_menu, "Manual", MenuApp_SelectManual);
    MF_AddAction(s_mode_menu, "Auto", MenuApp_SelectAuto);
    MF_AddValue(s_mode_menu,
                "Man Spd",
                &s_manual_speed_permille,
                0,
                1000,
                20,
                "",
                MenuApp_OnManualSpeedChanged);

    MF_AddSubmenu(s_root_menu, "Mode", s_mode_menu);
    MF_AddCustomPage(s_root_menu, "Info", MenuApp_RenderInfoPage);
    MF_AddValue(s_root_menu,
                "Safety",
                &s_safety_threshold_permille,
                0,
                1000,
                1,
                "",
                MenuApp_OnSafetyThresholdChanged);

    MF_SetCursorStyle(MF_CURSOR_REVERSE);
    MF_Start(s_root_menu);
}

void MenuApp_Init(void)
{
    OLED_Init();
    Key_Init();
    LED_Init();
    Buzzer_Init();
    MAX9814_Init();
    Motor_Init();
    Bluetooth_Init();

    s_mode = MENU_APP_MODE_MUSIC;
    s_manual_speed_permille = APP_DEFAULT_MANUAL_SPEED;
    s_safety_threshold_permille = APP_DEFAULT_SAFETY_THRESHOLD;
    s_target_duty_permille = 0u;
    s_applied_duty_permille = 0u;
    s_last_sound_level = 0u;
    s_last_raw_sound = 0u;
    s_alarm_active = 0u;
    s_alarm_suppress_until = 0u;
    s_alarm_output_on = 0u;
    s_bt_line_len = 0u;
    s_bt_last_rx_tick = HAL_GetTick();
    MenuApp_ResetAlarmWindow();
    MenuApp_SetAlarmOutputs(0u);
    Motor_Stop();

    MenuApp_BuildMenu();

    s_last_control_tick = HAL_GetTick();
    s_last_display_tick = s_last_control_tick;
    s_last_alarm_sample_tick = s_last_control_tick;
    s_last_alarm_blink_tick = s_last_control_tick;

    MF_Render();
}

void MenuApp_Task(void)
{
    uint32_t now = HAL_GetTick();
    KeyEvent_t key = Key_Scan();

    if ((key == KEY_BACK) && (s_alarm_active != 0u))
    {
        MenuApp_ClearAlarm();
    }

    MF_Process(key);

    MenuApp_ServiceBluetooth(now);
    MAX9814_Task();
    MenuApp_UpdateControl(now);
    MenuApp_UpdateAlarm(now);
    MenuApp_UpdateAlarmOutput(now);

    if ((uint32_t)(now - s_last_display_tick) >= APP_DISPLAY_PERIOD_MS)
    {
        s_last_display_tick = now;
        MF_Render();
    }
}
