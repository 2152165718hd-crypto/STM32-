#include "APPLICATION/Menu/Menu.h"

#include "APPLICATION/Menu/MenuFramework.h"
#include "APPLICATION/PVClean/PVClean.h"
#include "Hardware/ESP_01S/ESP_01S.h"
#include "Hardware/INA226/INA226.h"

#include <stdio.h>

#define MENU_REFRESH_PERIOD_MS 200u

static uint8_t g_menu_initialized = 0u;
static uint8_t g_menu_force_refresh = 0u;
static uint32_t g_menu_last_render_tick = 0u;

static void Menu_BuildTree(void);
static void Menu_RequestRefresh(void);
static void Menu_DrawTitle(const char *title);
static void Menu_FormatCurrent(char *buffer, uint32_t size, int32_t value_x100);
static const char *Menu_InaStageTag(uint8_t stage);
static void Menu_RenderStatusPage(KeyEvent_t key, uint8_t *exit_flag);
static void Menu_RenderWifiPage(KeyEvent_t key, uint8_t *exit_flag);
static void Menu_RenderSensorPage(KeyEvent_t key, uint8_t *exit_flag);
static void Menu_SetModeAuto(void);
static void Menu_SetModeManual(void);
static void Menu_SetModeTimer(void);
static void Menu_StartClean(void);
static void Menu_StopClean(void);
static void Menu_OnCommonConfigChanged(int32_t new_value);
static void Menu_OnTimerIntervalChanged(int32_t new_value);

void Menu_Init(void)
{
    OLED_Init();
    Key_Init();

    MF_Reset();
    Menu_BuildTree();

    g_menu_last_render_tick = 0u;
    g_menu_force_refresh = 1u;
    g_menu_initialized = 1u;
}

void Menu_Process(void)
{
    KeyEvent_t key;
    uint32_t now;

    if (g_menu_initialized == 0u)
    {
        return;
    }

    key = Key_Scan();
    now = HAL_GetTick();

    if (key != KEY_NONE)
    {
        MF_Process(key);
        g_menu_force_refresh = 1u;
    }

    if (g_menu_force_refresh || ((now - g_menu_last_render_tick) >= MENU_REFRESH_PERIOD_MS))
    {
        MF_Render();
        g_menu_last_render_tick = now;
        g_menu_force_refresh = 0u;
    }
}

static void Menu_BuildTree(void)
{
    PVCleanConfig_t *config = PVClean_GetConfig();
    MF_Menu_t *root_menu = MF_CreateMenu("PV Cleaner");
    MF_Menu_t *mode_menu = MF_CreateMenu("Mode");
    MF_Menu_t *auto_menu = MF_CreateMenu("Auto Settings");
    MF_Menu_t *timer_menu = MF_CreateMenu("Timer Settings");
    MF_Menu_t *manual_menu = MF_CreateMenu("Manual Control");

    MF_AddCustomPage(root_menu, "Status", Menu_RenderStatusPage);
    MF_AddSubmenu(root_menu, "Mode", mode_menu);
    MF_AddSubmenu(root_menu, "Auto Set", auto_menu);
    MF_AddSubmenu(root_menu, "Timer Set", timer_menu);
    MF_AddSubmenu(root_menu, "Manual", manual_menu);
    MF_AddCustomPage(root_menu, "Wi-Fi Info", Menu_RenderWifiPage);
    MF_AddCustomPage(root_menu, "Sensor Dbg", Menu_RenderSensorPage);

    MF_AddAction(mode_menu, "AUTO", Menu_SetModeAuto);
    MF_AddAction(mode_menu, "MANUAL", Menu_SetModeManual);
    MF_AddAction(mode_menu, "TIMER", Menu_SetModeTimer);

    MF_AddValue(auto_menu, "Night Th", &config->night_threshold_percent, 0, 100, 1, 0u, "%", Menu_OnCommonConfigChanged);
    MF_AddValue(auto_menu, "Auto Th", &config->auto_light_threshold_percent, 1, 100, 1, 0u, "%", Menu_OnCommonConfigChanged);
    MF_AddValue(auto_menu, "Max I", &config->max_expected_current_mA_x10, 1, 600, 1, 1u, "mA", Menu_OnCommonConfigChanged);
    MF_AddValue(auto_menu, "Dirty %", &config->dirty_ratio_percent, 10, 100, 1, 0u, "%", Menu_OnCommonConfigChanged);
    MF_AddValue(auto_menu, "Confirm", &config->confirm_seconds, 1, 60, 1, 0u, "s", Menu_OnCommonConfigChanged);

    MF_AddValue(timer_menu, "Interval", &config->timer_interval_h, 1, 72, 1, 0u, "h", Menu_OnTimerIntervalChanged);
    MF_AddValue(timer_menu, "Clean Sec", &config->clean_duration_sec, 1, 120, 1, 0u, "s", Menu_OnCommonConfigChanged);
    MF_AddValue(timer_menu, "Motor %", &config->motor_speed_percent, 10, 100, 5, 0u, "%", Menu_OnCommonConfigChanged);

    MF_AddAction(manual_menu, "Start Clean", Menu_StartClean);
    MF_AddAction(manual_menu, "Stop Clean", Menu_StopClean);

    MF_Start(root_menu);
}

static void Menu_RequestRefresh(void)
{
    g_menu_force_refresh = 1u;
}

static void Menu_DrawTitle(const char *title)
{
    OLED_ShowString(0, 0, (char *)title, OLED_6X8);
    OLED_DrawLine(0, 9, 127, 9);
}

static void Menu_FormatCurrent(char *buffer, uint32_t size, int32_t value_x100)
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

static const char *Menu_InaStageTag(uint8_t stage)
{
    switch ((INA226_DiagStage_t)stage)
    {
    case INA226_DIAG_STAGE_NONE:
        return "OK";
    case INA226_DIAG_STAGE_I2C_INIT:
        return "I2C";
    case INA226_DIAG_STAGE_SCAN_ADDRESS:
        return "ADR";
    case INA226_DIAG_STAGE_WRITE_CONFIG:
        return "CFG";
    case INA226_DIAG_STAGE_WRITE_CALIBRATION:
        return "CAL";
    case INA226_DIAG_STAGE_READ_CONFIG:
        return "RCF";
    case INA226_DIAG_STAGE_READ_CALIBRATION:
        return "RCL";
    case INA226_DIAG_STAGE_READ_CURRENT:
        return "CUR";
    case INA226_DIAG_STAGE_READ_BUS:
        return "BUS";
    case INA226_DIAG_STAGE_READ_SHUNT:
        return "SHU";
    case INA226_DIAG_STAGE_ENSURE_CALIB_READ:
        return "ECR";
    case INA226_DIAG_STAGE_ENSURE_CALIB_WRITE:
        return "ECW";
    default:
        return "UNK";
    }
}

static void Menu_RenderStatusPage(KeyEvent_t key, uint8_t *exit_flag)
{
    PVCleanStatus_t status;
    char current_text[16];
    char expected_text[16];
    char line[24];

    if ((key == KEY_BACK) && (exit_flag != NULL))
    {
        *exit_flag = 1u;
        return;
    }

    if (key != KEY_NONE)
    {
        return;
    }

    PVClean_GetStatus(&status);
    Menu_FormatCurrent(current_text, sizeof(current_text), status.current_mA_x100);
    Menu_FormatCurrent(expected_text, sizeof(expected_text), status.expected_current_mA_x100);

    Menu_DrawTitle("STATUS");
    snprintf(line, sizeof(line), "M:%s S:%s", PVClean_ModeString(status.mode), PVClean_StateString(status.clean_state));
    OLED_ShowString(0, 12, line, OLED_6X8);

    snprintf(line, sizeof(line), "L:%u%% R:%u", status.light_percent, status.light_raw);
    OLED_ShowString(0, 20, line, OLED_6X8);

    snprintf(line, sizeof(line), "I:%s E:%s", current_text, expected_text);
    OLED_ShowString(0, 28, line, OLED_6X8);

    snprintf(line, sizeof(line), "Clean:%lus", (unsigned long)status.clean_remaining_sec);
    OLED_ShowString(0, 36, line, OLED_6X8);

    snprintf(line, sizeof(line), "T:%lus W:%s", (unsigned long)status.timer_remaining_sec, (status.wifi_state == ESP_CONN_CLIENT_CONNECTED) ? "LINK" : ((status.wifi_state == ESP_CONN_AP_READY) ? "AP" : "FAIL"));
    OLED_ShowString(0, 44, line, OLED_6X8);

    snprintf(line, sizeof(line), "N:%u P:%u B:Back", status.night_locked, status.timer_pending);
    OLED_ShowString(0, 52, line, OLED_6X8);
}

static void Menu_RenderWifiPage(KeyEvent_t key, uint8_t *exit_flag)
{
    PVCleanStatus_t status;
    char line[24];

    if ((key == KEY_BACK) && (exit_flag != NULL))
    {
        *exit_flag = 1u;
        return;
    }

    if (key != KEY_NONE)
    {
        return;
    }

    PVClean_GetStatus(&status);

    Menu_DrawTitle("WI-FI");
    snprintf(line, sizeof(line), "SSID:%s", ESP_WIFI_SSID);
    OLED_ShowString(0, 12, line, OLED_6X8);

    snprintf(line, sizeof(line), "PASS:%s", ESP_WIFI_PASS);
    OLED_ShowString(0, 20, line, OLED_6X8);

    snprintf(line, sizeof(line), "HOST:%s", ESP_AP_IP_ADDR);
    OLED_ShowString(0, 28, line, OLED_6X8);

    snprintf(line, sizeof(line), "PORT:%s TCP", ESP_SERVER_PORT);
    OLED_ShowString(0, 36, line, OLED_6X8);

    snprintf(line, sizeof(line), "TXT:1S PUSH C:%u", status.wifi_clients);
    OLED_ShowString(0, 44, line, OLED_6X8);

    snprintf(line, sizeof(line), "S:%s B:Back",
             (status.wifi_state == ESP_CONN_CLIENT_CONNECTED) ? "CLIENT" : ((status.wifi_state == ESP_CONN_AP_READY) ? "AP" : "FAIL"));
    OLED_ShowString(0, 52, line, OLED_6X8);
}

static void Menu_RenderSensorPage(KeyEvent_t key, uint8_t *exit_flag)
{
    PVCleanStatus_t status;
    char current_text[16];
    char line[24];

    if ((key == KEY_BACK) && (exit_flag != NULL))
    {
        *exit_flag = 1u;
        return;
    }

    if (key != KEY_NONE)
    {
        return;
    }

    PVClean_GetStatus(&status);
    Menu_FormatCurrent(current_text, sizeof(current_text), status.current_mA_x100);

    Menu_DrawTitle("SENSOR DBG");

    snprintf(line, sizeof(line), "AO:%u DO:%c", status.light_raw, status.light_digital ? 'H' : 'L');
    OLED_ShowString(0, 12, line, OLED_6X8);

    snprintf(line, sizeof(line), "B:%umV I:%s", status.bus_voltage_mV, current_text);
    OLED_ShowString(0, 20, line, OLED_6X8);

    snprintf(line, sizeof(line), "Sh:%lduV A:%c", (long)status.shunt_uV, status.alert_level ? 'H' : 'L');
    OLED_ShowString(0, 28, line, OLED_6X8);

    snprintf(line, sizeof(line), "R:%c V:%c A:%02Xh",
             status.ina_ready ? 'O' : 'X',
             status.ina_data_valid ? 'O' : 'X',
             status.ina_addr_7bit);
    OLED_ShowString(0, 36, line, OLED_6X8);

    snprintf(line, sizeof(line), "S:%s H:%u E:%lX",
             Menu_InaStageTag(status.ina_last_stage),
             status.ina_last_hal_status,
             (unsigned long)status.ina_last_error_code);
    OLED_ShowString(0, 44, line, OLED_6X8);

    OLED_ShowString(0, 52, "B:Back", OLED_6X8);
}

static void Menu_SetModeAuto(void)
{
    PVClean_SetMode(PVCLEAN_MODE_AUTO);
    Menu_RequestRefresh();
}

static void Menu_SetModeManual(void)
{
    PVClean_SetMode(PVCLEAN_MODE_MANUAL);
    Menu_RequestRefresh();
}

static void Menu_SetModeTimer(void)
{
    PVClean_SetMode(PVCLEAN_MODE_TIMER);
    Menu_RequestRefresh();
}

static void Menu_StartClean(void)
{
    PVClean_RequestManualStart();
    Menu_RequestRefresh();
}

static void Menu_StopClean(void)
{
    PVClean_RequestStop();
    Menu_RequestRefresh();
}

static void Menu_OnCommonConfigChanged(int32_t new_value)
{
    (void)new_value;
    PVClean_ApplyConfig();
    Menu_RequestRefresh();
}

static void Menu_OnTimerIntervalChanged(int32_t new_value)
{
    (void)new_value;
    PVClean_ApplyConfig();
    PVClean_ResetTimerSchedule();
    Menu_RequestRefresh();
}
