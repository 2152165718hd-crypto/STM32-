#include ".\APPLICATION\Menu\Menu.h"

#include <stdio.h>
#include <string.h>

#include ".\APPLICATION\Menu\MenuFramework.h"
#include ".\Hardware\Buzzer\Buzzer.h"
#include ".\Hardware\DS18B20\DS18B20.h"
#include ".\Hardware\ESP_01S\ESP_01S.h"
#include ".\Hardware\FAN\FAN.h"
#include ".\Hardware\LED\LED.h"
#include ".\Hardware\Motor\Motor.h"
#include ".\Hardware\OLED\OLED.h"
#include ".\Hardware\VL53L0X\VL53L0X.h"

#define APP_TEMP_SAMPLE_PERIOD_MS      1500u
#define APP_TEMP_CONVERT_MS             750u
#define APP_DISTANCE_SAMPLE_PERIOD_MS   300u
#define APP_OBSTACLE_SAMPLE_PERIOD_MS    50u
#define APP_NOTICE_HOLD_MS             2000u
#define APP_MESSAGE_HOLD_MS             600u
#define APP_TEMP_HYSTERESIS_C             2

#define APP_DOOR_DIR_SIGN                1
#define APP_DOOR_SPEED_OPEN  (APP_DOOR_DIR_SIGN * 80)
#define APP_DOOR_SPEED_CLOSE (-APP_DOOR_DIR_SIGN * 80)
#define APP_DOOR_SPEED_BACK  (-APP_DOOR_DIR_SIGN * 60)

typedef enum
{
    DOOR_CLOSED = 0,
    DOOR_OPENING,
    DOOR_OPEN,
    DOOR_CLOSING,
    DOOR_BLOCKED,
    DOOR_FAULT
} DoorState_t;

typedef enum
{
    TEMP_IDLE = 0,
    TEMP_WAIT_CONVERT
} TempStage_t;

typedef struct
{
    uint8_t active;
    uint8_t phase_on;
    uint8_t pulses_left;
    uint16_t on_ms;
    uint16_t off_ms;
    uint32_t deadline_ms;
} BuzzerPattern_t;

typedef struct
{
    uint8_t self_test_mode;
    uint8_t temp_alarm;
    uint8_t retreating;
    DoorState_t door_state;
    TempStage_t temp_stage;
    int16_t temp_deci_c;
    uint16_t distance_mm;
    int32_t temp_threshold_c;
    int32_t auto_close_s;
    int32_t obstacle_threshold_mm;
    int32_t retreat_time_ms;
    int32_t open_run_ms;
    int32_t close_run_ms;
    uint32_t motion_start_ms;
    uint32_t open_since_ms;
    uint32_t retreat_end_ms;
    uint32_t temp_stage_start_ms;
    uint32_t last_temp_sample_ms;
    uint32_t last_distance_sample_ms;
    uint32_t last_obstacle_poll_ms;
    uint32_t notice_until_ms;
    char last_command[24];
    char last_fault[24];
    char notice[40];
} ATM_State_t;

static ATM_State_t s_state;
static BuzzerPattern_t s_buzzer;

static MF_Menu_t *s_root_menu = NULL;

static int Menu_StrPixelWidth(const char *s);
static void Menu_DrawTitleBar(const char *title);
static void Menu_ShowMessageNow(const char *text, uint16_t hold_ms);
static void Menu_SetNotice(const char *text);
static void Menu_ClearNotice(void);
static void Menu_FormatTemperature(char *buf, size_t size);
static void Menu_CopyDistanceText(char *buf, size_t size);
static const char *Menu_GetModeTextCn(void);
static const char *Menu_GetModeTextAscii(void);
static const char *Menu_GetDoorTextAscii(void);
static const char *Menu_GetApStateText(void);
static const char *Menu_GetOnOffText(uint8_t on);
static void Menu_UpdateOutputs(void);
static void Menu_StartBuzzerPattern(uint8_t pulses, uint16_t on_ms, uint16_t off_ms);
static void Menu_ServiceBuzzer(void);
static void Menu_SendLineToLink(uint8_t link_id, const char *line);
static void Menu_SendAsyncLine(const char *line);
static void Menu_SendStatusToLink(uint8_t link_id);
static void Menu_SendStatusIfConnected(void);
static void Menu_EvaluateTemperatureAlarm(void);
static void Menu_SetMode(uint8_t self_test_mode);
static uint8_t Menu_RequestOpenDoor(void);
static uint8_t Menu_RequestCloseDoor(void);
static void Menu_StopDoor(void);
static void Menu_ClearFault(void);
static void Menu_HandleObstacle(void);
static void Menu_ServiceDoor(void);
static void Menu_ServiceTemperature(void);
static void Menu_ServiceDistance(void);
static void Menu_NormalizeCommand(char *cmd);
static void Menu_ProcessCommand(char *cmd, uint8_t link_id);
static void Menu_OnEspPacket(ESP_DataPacket_t *packet);
static void Menu_OnModeToggle(void);
static void Menu_OnTempThresholdChanged(int32_t new_value);
static void Menu_ActionOpenDoor(void);
static void Menu_ActionCloseDoor(void);
static void Menu_ActionStopDoor(void);
static void Menu_ActionClearFault(void);
static void Menu_ActionLedTest(void);
static void Menu_ActionBuzzerTest(void);
static void Menu_ActionFanTest(void);
static void Menu_ActionDoorTest(void);
static void Menu_ActionDistanceRead(void);
static void Menu_ActionTempRead(void);
static void Menu_PageOverview(KeyEvent_t key, uint8_t *exit_flag);
static void Menu_PageCommStatus(KeyEvent_t key, uint8_t *exit_flag);
static void Menu_BuildTree(void);

static int Menu_StrPixelWidth(const char *s)
{
    int width = 0;

    while (*s != '\0')
    {
        uint8_t ch = (uint8_t)*s;

        if ((ch & 0x80u) == 0u)
        {
            width += 8;
            s++;
        }
        else if ((ch & 0xE0u) == 0xC0u)
        {
            width += 16;
            s += 2;
        }
        else if ((ch & 0xF0u) == 0xE0u)
        {
            width += 16;
            s += 3;
        }
        else if ((ch & 0xF8u) == 0xF0u)
        {
            width += 16;
            s += 4;
        }
        else
        {
            s++;
        }
    }

    return width;
}

static void Menu_DrawTitleBar(const char *title)
{
    int width = Menu_StrPixelWidth(title);
    int x = (128 - width) / 2;

    if (x < 0)
    {
        x = 0;
    }

    OLED_ShowString((int16_t)x, 0, (char *)title, OLED_8X16);
    OLED_DrawLine(0, 15, 127, 15);
}

static void Menu_ShowMessageNow(const char *text, uint16_t hold_ms)
{
    int width = Menu_StrPixelWidth(text);
    int x = (128 - width) / 2;

    if (x < 0)
    {
        x = 0;
    }

    OLED_Clear();
    OLED_ShowString((int16_t)x, 24, (char *)text, OLED_8X16);
    OLED_Update();
    HAL_Delay(hold_ms);
}

static void Menu_SetNotice(const char *text)
{
    strncpy(s_state.notice, text, sizeof(s_state.notice) - 1u);
    s_state.notice[sizeof(s_state.notice) - 1u] = '\0';
    s_state.notice_until_ms = HAL_GetTick() + APP_NOTICE_HOLD_MS;
}

static void Menu_ClearNotice(void)
{
    s_state.notice[0] = '\0';
    s_state.notice_until_ms = 0u;
}

static void Menu_FormatTemperature(char *buf, size_t size)
{
    uint16_t abs_temp;

    if (s_state.temp_deci_c == (int16_t)DS18B20_TEMP_ERROR)
    {
        strncpy(buf, "NA", size - 1u);
        buf[size - 1u] = '\0';
        return;
    }

    if (s_state.temp_deci_c < 0)
    {
        abs_temp = (uint16_t)(-s_state.temp_deci_c);
        snprintf(buf, size, "-%u.%u", abs_temp / 10u, abs_temp % 10u);
    }
    else
    {
        abs_temp = (uint16_t)s_state.temp_deci_c;
        snprintf(buf, size, "%u.%u", abs_temp / 10u, abs_temp % 10u);
    }
}

static void Menu_CopyDistanceText(char *buf, size_t size)
{
    if (s_state.distance_mm == VL53L0X_DISTANCE_INVALID)
    {
        strncpy(buf, "NA", size - 1u);
        buf[size - 1u] = '\0';
    }
    else
    {
        snprintf(buf, size, "%u", (unsigned int)s_state.distance_mm);
    }
}

static const char *Menu_GetModeTextCn(void)
{
    return (s_state.self_test_mode != 0u) ? "自检" : "加钞";
}

static const char *Menu_GetModeTextAscii(void)
{
    return (s_state.self_test_mode != 0u) ? "SELFTEST" : "LOAD";
}

static const char *Menu_GetDoorTextAscii(void)
{
    switch (s_state.door_state)
    {
    case DOOR_OPENING:
        return "OPG";
    case DOOR_OPEN:
        return "OPEN";
    case DOOR_CLOSING:
        return "CLG";
    case DOOR_BLOCKED:
        return "BLK";
    case DOOR_FAULT:
        return "FLT";
    case DOOR_CLOSED:
    default:
        return "CLS";
    }
}

static const char *Menu_GetApStateText(void)
{
    ESP_ConnectionState_t state = ESP_GetConnectionState();

    if (state == ESP_CONN_CLIENT_CONNECTED)
    {
        return "CLIENT";
    }
    if (state == ESP_CONN_AP_READY)
    {
        return "READY";
    }
    return "INIT_FAIL";
}

static const char *Menu_GetOnOffText(uint8_t on)
{
    return (on != 0u) ? "ON" : "OFF";
}

static void Menu_UpdateOutputs(void)
{
    FAN_Set(s_state.temp_alarm);
    LED_Set(LED_ALARM, s_state.temp_alarm);
    LED_Set(LED_GATE, (s_state.door_state != DOOR_CLOSED) ? 1u : 0u);
}

static void Menu_StartBuzzerPattern(uint8_t pulses, uint16_t on_ms, uint16_t off_ms)
{
    if (pulses == 0u)
    {
        Buzzer_Off();
        memset(&s_buzzer, 0, sizeof(s_buzzer));
        return;
    }

    s_buzzer.active = 1u;
    s_buzzer.phase_on = 1u;
    s_buzzer.pulses_left = pulses;
    s_buzzer.on_ms = on_ms;
    s_buzzer.off_ms = off_ms;
    s_buzzer.deadline_ms = HAL_GetTick() + on_ms;
    Buzzer_On();
}

static void Menu_ServiceBuzzer(void)
{
    uint32_t now = HAL_GetTick();

    if (s_buzzer.active == 0u || now < s_buzzer.deadline_ms)
    {
        return;
    }

    if (s_buzzer.phase_on != 0u)
    {
        Buzzer_Off();
        if (s_buzzer.pulses_left > 0u)
        {
            s_buzzer.pulses_left--;
        }

        if (s_buzzer.pulses_left == 0u)
        {
            s_buzzer.active = 0u;
            return;
        }

        s_buzzer.phase_on = 0u;
        s_buzzer.deadline_ms = now + s_buzzer.off_ms;
    }
    else
    {
        Buzzer_On();
        s_buzzer.phase_on = 1u;
        s_buzzer.deadline_ms = now + s_buzzer.on_ms;
    }
}

static void Menu_SendLineToLink(uint8_t link_id, const char *line)
{
    char tx_buf[192];

    if (ESP_GetConnectionState() == ESP_CONN_INIT_FAIL || line == NULL)
    {
        return;
    }

    snprintf(tx_buf, sizeof(tx_buf), "%s\r\n", line);
    (void)ESP_SendString(link_id, tx_buf);
}

static void Menu_SendAsyncLine(const char *line)
{
    if (ESP_GetConnectionState() == ESP_CONN_CLIENT_CONNECTED)
    {
        Menu_SendLineToLink(ESP_GetActiveLinkId(), line);
    }
}

static void Menu_SendStatusToLink(uint8_t link_id)
{
    char temp_buf[16];
    char dist_buf[16];
    char line[192];

    Menu_FormatTemperature(temp_buf, sizeof(temp_buf));
    Menu_CopyDistanceText(dist_buf, sizeof(dist_buf));

    snprintf(line, sizeof(line),
             "STATE:MODE=%s,DOOR=%s,TEMP=%s,FAN=%s,ALARM=%s,DIST=%s",
             Menu_GetModeTextAscii(), Menu_GetDoorTextAscii(), temp_buf,
             Menu_GetOnOffText(FAN_GetState()), Menu_GetOnOffText(s_state.temp_alarm),
             dist_buf);
    Menu_SendLineToLink(link_id, line);
}

static void Menu_SendStatusIfConnected(void)
{
    if (ESP_GetConnectionState() == ESP_CONN_CLIENT_CONNECTED)
    {
        Menu_SendStatusToLink(ESP_GetActiveLinkId());
    }
}

static void Menu_EvaluateTemperatureAlarm(void)
{
    int16_t threshold_high = (int16_t)(s_state.temp_threshold_c * 10);
    int16_t threshold_low = (int16_t)((s_state.temp_threshold_c - APP_TEMP_HYSTERESIS_C) * 10);

    if (s_state.temp_deci_c == (int16_t)DS18B20_TEMP_ERROR)
    {
        Menu_UpdateOutputs();
        return;
    }

    if (s_state.temp_alarm == 0u && s_state.temp_deci_c >= threshold_high)
    {
        s_state.temp_alarm = 1u;
        Menu_SendAsyncLine("EVENT:TEMP_HIGH");
    }
    else if (s_state.temp_alarm != 0u && s_state.temp_deci_c <= threshold_low)
    {
        s_state.temp_alarm = 0u;
        Menu_SendAsyncLine("EVENT:TEMP_NORMAL");
    }

    Menu_UpdateOutputs();
}

static void Menu_SetMode(uint8_t self_test_mode)
{
    s_state.self_test_mode = (self_test_mode != 0u) ? 1u : 0u;
    Menu_SendStatusIfConnected();
}

static uint8_t Menu_RequestOpenDoor(void)
{
    if (s_state.door_state == DOOR_BLOCKED || s_state.door_state == DOOR_FAULT)
    {
        return 0u;
    }

    if (s_state.door_state == DOOR_OPEN || s_state.door_state == DOOR_OPENING)
    {
        return 1u;
    }

    s_state.retreating = 0u;
    s_state.door_state = DOOR_OPENING;
    s_state.motion_start_ms = HAL_GetTick();
    s_state.last_obstacle_poll_ms = 0u;
    Motor_SetDoorSpeed(APP_DOOR_SPEED_OPEN);
    Menu_UpdateOutputs();
    return 1u;
}

static uint8_t Menu_RequestCloseDoor(void)
{
    if (s_state.door_state == DOOR_BLOCKED || s_state.door_state == DOOR_FAULT)
    {
        return 0u;
    }

    if (s_state.door_state == DOOR_CLOSED || s_state.door_state == DOOR_CLOSING)
    {
        return 1u;
    }

    s_state.retreating = 0u;
    s_state.door_state = DOOR_CLOSING;
    s_state.motion_start_ms = HAL_GetTick();
    s_state.open_since_ms = 0u;
    Motor_SetDoorSpeed(APP_DOOR_SPEED_CLOSE);
    Menu_UpdateOutputs();
    return 1u;
}

static void Menu_StopDoor(void)
{
    Motor_StopAll();

    if (s_state.door_state == DOOR_OPENING)
    {
        s_state.door_state = DOOR_OPEN;
        s_state.open_since_ms = HAL_GetTick();
    }
    else if (s_state.door_state == DOOR_CLOSING)
    {
        s_state.door_state = DOOR_CLOSED;
        s_state.open_since_ms = 0u;
    }

    Menu_UpdateOutputs();
}

static void Menu_ClearFault(void)
{
    Motor_StopAll();
    s_state.retreating = 0u;
    s_state.door_state = DOOR_CLOSED;
    s_state.open_since_ms = 0u;
    strncpy(s_state.last_fault, "NONE", sizeof(s_state.last_fault) - 1u);
    s_state.last_fault[sizeof(s_state.last_fault) - 1u] = '\0';
    Menu_ClearNotice();
    Menu_UpdateOutputs();
    Menu_SendStatusIfConnected();
}

static void Menu_HandleObstacle(void)
{
    strncpy(s_state.last_fault, "OBSTACLE", sizeof(s_state.last_fault) - 1u);
    s_state.last_fault[sizeof(s_state.last_fault) - 1u] = '\0';
    s_state.door_state = DOOR_BLOCKED;
    s_state.retreating = 1u;
    s_state.retreat_end_ms = HAL_GetTick() + (uint32_t)s_state.retreat_time_ms;
    s_state.open_since_ms = 0u;
    Motor_SetDoorSpeed(APP_DOOR_SPEED_BACK);
    Menu_SetNotice("闸口受阻");
    Menu_ShowMessageNow("闸口受阻", APP_MESSAGE_HOLD_MS);
    Menu_SendAsyncLine("FAULT:OBSTACLE");
    Menu_UpdateOutputs();
}

static void Menu_ServiceDoor(void)
{
    uint32_t now = HAL_GetTick();

    if (s_state.notice_until_ms != 0u && now >= s_state.notice_until_ms)
    {
        Menu_ClearNotice();
    }

    if (s_state.door_state == DOOR_BLOCKED && s_state.retreating != 0u)
    {
        if (now >= s_state.retreat_end_ms)
        {
            s_state.retreating = 0u;
            Motor_StopAll();
            Menu_UpdateOutputs();
        }
        return;
    }

    if (s_state.door_state == DOOR_OPENING)
    {
        if ((now - s_state.last_obstacle_poll_ms) >= APP_OBSTACLE_SAMPLE_PERIOD_MS)
        {
            uint16_t distance = VL53L0X_ReadDistanceMm();

            s_state.last_obstacle_poll_ms = now;
            if (distance != VL53L0X_DISTANCE_INVALID)
            {
                s_state.distance_mm = distance;
                if (distance < (uint16_t)s_state.obstacle_threshold_mm)
                {
                    Menu_HandleObstacle();
                    return;
                }
            }
        }

        if ((now - s_state.motion_start_ms) >= (uint32_t)s_state.open_run_ms)
        {
            Motor_StopAll();
            s_state.door_state = DOOR_OPEN;
            s_state.open_since_ms = now;
            Menu_UpdateOutputs();
        }
        return;
    }

    if (s_state.door_state == DOOR_CLOSING)
    {
        if ((now - s_state.motion_start_ms) >= (uint32_t)s_state.close_run_ms)
        {
            Motor_StopAll();
            s_state.door_state = DOOR_CLOSED;
            s_state.open_since_ms = 0u;
            Menu_UpdateOutputs();
        }
        return;
    }

    if (s_state.door_state == DOOR_OPEN &&
        s_state.auto_close_s > 0 &&
        s_state.open_since_ms != 0u &&
        (now - s_state.open_since_ms) >= (uint32_t)s_state.auto_close_s * 1000u)
    {
        Menu_StartBuzzerPattern(3u, 100u, 100u);
        (void)Menu_RequestCloseDoor();
        Menu_SetNotice("已超时，自动关闭");
        Menu_ShowMessageNow("已超时，自动关闭", APP_MESSAGE_HOLD_MS);
        Menu_SendAsyncLine("EVENT:AUTO_CLOSE");
    }
}

static void Menu_ServiceTemperature(void)
{
    uint32_t now = HAL_GetTick();

    if (s_state.temp_stage == TEMP_IDLE)
    {
        if ((now - s_state.last_temp_sample_ms) >= APP_TEMP_SAMPLE_PERIOD_MS)
        {
            if (DS18B20_IsReady() != 0u || DS18B20_Init() != 0u)
            {
                if (DS18B20_StartConversion() != 0u)
                {
                    s_state.temp_stage = TEMP_WAIT_CONVERT;
                    s_state.temp_stage_start_ms = now;
                }
            }
            s_state.last_temp_sample_ms = now;
        }
    }
    else if ((now - s_state.temp_stage_start_ms) >= APP_TEMP_CONVERT_MS)
    {
        float temp = DS18B20_ReadTemperatureResult();

        s_state.temp_stage = TEMP_IDLE;
        if (temp != DS18B20_TEMP_ERROR)
        {
            if (temp >= 0.0f)
            {
                s_state.temp_deci_c = (int16_t)(temp * 10.0f + 0.5f);
            }
            else
            {
                s_state.temp_deci_c = (int16_t)(temp * 10.0f - 0.5f);
            }
            Menu_EvaluateTemperatureAlarm();
        }
    }
}

static void Menu_ServiceDistance(void)
{
    uint32_t now = HAL_GetTick();

    if (s_state.door_state == DOOR_OPENING || s_state.retreating != 0u)
    {
        return;
    }

    if ((now - s_state.last_distance_sample_ms) >= APP_DISTANCE_SAMPLE_PERIOD_MS)
    {
        uint16_t distance = VL53L0X_ReadDistanceMm();

        s_state.last_distance_sample_ms = now;
        if (distance != VL53L0X_DISTANCE_INVALID)
        {
            s_state.distance_mm = distance;
        }
    }
}

static void Menu_NormalizeCommand(char *cmd)
{
    size_t len;
    size_t start;
    size_t end;
    size_t i;

    len = strlen(cmd);
    while (len > 0u && (cmd[len - 1u] == '\r' || cmd[len - 1u] == '\n' ||
                        cmd[len - 1u] == ' ' || cmd[len - 1u] == '\t'))
    {
        cmd[len - 1u] = '\0';
        len--;
    }

    start = 0u;
    while (cmd[start] == ' ' || cmd[start] == '\t')
    {
        start++;
    }

    if (start > 0u)
    {
        end = 0u;
        while (cmd[start] != '\0')
        {
            cmd[end++] = cmd[start++];
        }
        cmd[end] = '\0';
    }

    for (i = 0u; cmd[i] != '\0'; i++)
    {
        if (cmd[i] >= 'a' && cmd[i] <= 'z')
        {
            cmd[i] = (char)(cmd[i] - 'a' + 'A');
        }
    }
}

static void Menu_ProcessCommand(char *cmd, uint8_t link_id)
{
    char ack[64];

    strncpy(s_state.last_command, cmd, sizeof(s_state.last_command) - 1u);
    s_state.last_command[sizeof(s_state.last_command) - 1u] = '\0';

    if (strcmp(cmd, "OPEN") == 0)
    {
        if (Menu_RequestOpenDoor() != 0u)
        {
            snprintf(ack, sizeof(ack), "ACK:%s", Menu_GetDoorTextAscii());
            Menu_SendLineToLink(link_id, ack);
            Menu_SendStatusToLink(link_id);
        }
        else
        {
            Menu_SendLineToLink(link_id, "ERR:FAULT");
        }
    }
    else if (strcmp(cmd, "CLOSE") == 0)
    {
        if (Menu_RequestCloseDoor() != 0u)
        {
            snprintf(ack, sizeof(ack), "ACK:%s", Menu_GetDoorTextAscii());
            Menu_SendLineToLink(link_id, ack);
            Menu_SendStatusToLink(link_id);
        }
        else
        {
            Menu_SendLineToLink(link_id, "ERR:FAULT");
        }
    }
    else if (strcmp(cmd, "MODE:LOAD") == 0)
    {
        Menu_SetMode(0u);
        Menu_SendLineToLink(link_id, "ACK:MODE=LOAD");
        Menu_SendStatusToLink(link_id);
    }
    else if (strcmp(cmd, "MODE:SELFTEST") == 0)
    {
        Menu_SetMode(1u);
        Menu_SendLineToLink(link_id, "ACK:MODE=SELFTEST");
        Menu_SendStatusToLink(link_id);
    }
    else if (strcmp(cmd, "STATUS?") == 0)
    {
        Menu_SendStatusToLink(link_id);
    }
    else if (strcmp(cmd, "CLRFAULT") == 0)
    {
        Menu_ClearFault();
        Menu_SendLineToLink(link_id, "ACK:CLEAR");
        Menu_SendStatusToLink(link_id);
    }
    else
    {
        Menu_SendLineToLink(link_id, "ERR:UNKNOWN");
    }
}

static void Menu_OnEspPacket(ESP_DataPacket_t *packet)
{
    char cmd[ESP_DATA_BUF_SIZE + 1u];
    uint16_t copy_len;

    if (packet == NULL || packet->len == 0u)
    {
        return;
    }

    copy_len = packet->len;
    if (copy_len > ESP_DATA_BUF_SIZE)
    {
        copy_len = ESP_DATA_BUF_SIZE;
    }

    memcpy(cmd, packet->data, copy_len);
    cmd[copy_len] = '\0';
    Menu_NormalizeCommand(cmd);
    Menu_ProcessCommand(cmd, packet->link_id);
}

static void Menu_OnModeToggle(void)
{
    Menu_SetMode(s_state.self_test_mode);
}

static void Menu_OnTempThresholdChanged(int32_t new_value)
{
    (void)new_value;
    Menu_EvaluateTemperatureAlarm();
}

static void Menu_ActionOpenDoor(void)
{
    (void)Menu_RequestOpenDoor();
}

static void Menu_ActionCloseDoor(void)
{
    (void)Menu_RequestCloseDoor();
}

static void Menu_ActionStopDoor(void)
{
    Menu_StopDoor();
}

static void Menu_ActionClearFault(void)
{
    Menu_ClearFault();
}

static void Menu_ActionLedTest(void)
{
    LED_Set(LED_GATE, 1u);
    LED_Set(LED_ALARM, 1u);
    HAL_Delay(300u);
    Menu_UpdateOutputs();
}

static void Menu_ActionBuzzerTest(void)
{
    Menu_StartBuzzerPattern(1u, 200u, 0u);
}

static void Menu_ActionFanTest(void)
{
    FAN_Set(1u);
    HAL_Delay(400u);
    Menu_UpdateOutputs();
}

static void Menu_ActionDoorTest(void)
{
    if (s_state.door_state == DOOR_CLOSED)
    {
        (void)Menu_RequestOpenDoor();
    }
    else
    {
        (void)Menu_RequestCloseDoor();
    }
}

static void Menu_ActionDistanceRead(void)
{
    char msg[32];
    uint16_t distance = VL53L0X_ReadDistanceMm();

    if (distance == VL53L0X_DISTANCE_INVALID)
    {
        strncpy(msg, "DIST:NA", sizeof(msg) - 1u);
        msg[sizeof(msg) - 1u] = '\0';
    }
    else
    {
        s_state.distance_mm = distance;
        snprintf(msg, sizeof(msg), "DIST:%umm", (unsigned int)distance);
    }

    Menu_ShowMessageNow(msg, APP_MESSAGE_HOLD_MS);
}

static void Menu_ActionTempRead(void)
{
    char msg[32];
    float temp = DS18B20_ReadTemperature();

    if (temp == DS18B20_TEMP_ERROR)
    {
        strncpy(msg, "TEMP:NA", sizeof(msg) - 1u);
        msg[sizeof(msg) - 1u] = '\0';
    }
    else
    {
        if (temp >= 0.0f)
        {
            s_state.temp_deci_c = (int16_t)(temp * 10.0f + 0.5f);
        }
        else
        {
            s_state.temp_deci_c = (int16_t)(temp * 10.0f - 0.5f);
        }
        Menu_FormatTemperature(msg, sizeof(msg));
        snprintf(msg, sizeof(msg), "TEMP:%d.%dC",
                 (int)(s_state.temp_deci_c / 10),
                 (int)((s_state.temp_deci_c < 0 ? -s_state.temp_deci_c : s_state.temp_deci_c) % 10));
        Menu_EvaluateTemperatureAlarm();
    }

    Menu_ShowMessageNow(msg, APP_MESSAGE_HOLD_MS);
}

static void Menu_PageOverview(KeyEvent_t key, uint8_t *exit_flag)
{
    char line1[32];
    char line2[40];
    char line3[40];
    char temp_buf[16];
    char dist_buf[16];

    if (key == KEY_BACK)
    {
        *exit_flag = 1u;
        return;
    }

    if (key != KEY_NONE)
    {
        return;
    }

    Menu_DrawTitleBar("运行总览");
    Menu_FormatTemperature(temp_buf, sizeof(temp_buf));
    Menu_CopyDistanceText(dist_buf, sizeof(dist_buf));

    snprintf(line1, sizeof(line1), "模式:%s", Menu_GetModeTextCn());
    if (s_state.notice[0] != '\0')
    {
        strncpy(line2, s_state.notice, sizeof(line2) - 1u);
        line2[sizeof(line2) - 1u] = '\0';
    }
    else
    {
        snprintf(line2, sizeof(line2), "门:%s D:%s", Menu_GetDoorTextAscii(), dist_buf);
    }
    snprintf(line3, sizeof(line3), "温:%s 风:%s", temp_buf, Menu_GetOnOffText(FAN_GetState()));

    OLED_ShowString(0, 16, line1, OLED_8X16);
    OLED_ShowString(0, 32, line2, OLED_8X16);
    OLED_ShowString(0, 48, line3, OLED_8X16);
}

static void Menu_PageCommStatus(KeyEvent_t key, uint8_t *exit_flag)
{
    char line1[32];
    char line2[32];
    char line3[40];

    if (key == KEY_BACK)
    {
        *exit_flag = 1u;
        return;
    }

    if (key != KEY_NONE)
    {
        return;
    }

    Menu_DrawTitleBar("通信状态");
    snprintf(line1, sizeof(line1), "AP:%s", Menu_GetApStateText());
    snprintf(line2, sizeof(line2), "LK:%u %s", (unsigned int)ESP_GetActiveLinkId(), s_state.last_command);
    snprintf(line3, sizeof(line3), "FLT:%s", s_state.last_fault);

    OLED_ShowString(0, 16, line1, OLED_8X16);
    OLED_ShowString(0, 32, line2, OLED_8X16);
    OLED_ShowString(0, 48, line3, OLED_8X16);
}

static void Menu_BuildTree(void)
{
    MF_Menu_t *mode_menu;
    MF_Menu_t *door_menu;
    MF_Menu_t *param_menu;
    MF_Menu_t *test_menu;

    MF_Reset();

    s_root_menu = MF_CreateMenu("ATM MENU");
    mode_menu = MF_CreateMenu("维护模式");
    door_menu = MF_CreateMenu("闸门控制");
    param_menu = MF_CreateMenu("参数设置");
    test_menu = MF_CreateMenu("自检测试");

    MF_AddCustomPage(s_root_menu, "运行总览", Menu_PageOverview);
    MF_AddSubmenu(s_root_menu, "维护模式", mode_menu);
    MF_AddSubmenu(s_root_menu, "闸门控制", door_menu);
    MF_AddSubmenu(s_root_menu, "参数设置", param_menu);
    MF_AddSubmenu(s_root_menu, "自检测试", test_menu);
    MF_AddCustomPage(s_root_menu, "通信状态", Menu_PageCommStatus);

    MF_AddToggle(mode_menu, "自检模式", &s_state.self_test_mode, Menu_OnModeToggle);

    MF_AddAction(door_menu, "点动开闸", Menu_ActionOpenDoor);
    MF_AddAction(door_menu, "点动关闸", Menu_ActionCloseDoor);
    MF_AddAction(door_menu, "停止电机", Menu_ActionStopDoor);
    MF_AddAction(door_menu, "清除故障", Menu_ActionClearFault);

    MF_AddValue(param_menu, "高温阈值", &s_state.temp_threshold_c, 20, 80, 1, "C",
                Menu_OnTempThresholdChanged);
    MF_AddValue(param_menu, "自动关门", &s_state.auto_close_s, 5, 120, 1, "s", NULL);
    MF_AddValue(param_menu, "受阻阈值", &s_state.obstacle_threshold_mm, 30, 400, 10, "mm", NULL);
    MF_AddValue(param_menu, "退回时间", &s_state.retreat_time_ms, 200, 5000, 100, "ms", NULL);
    MF_AddValue(param_menu, "开门行程", &s_state.open_run_ms, 200, 5000, 100, "ms", NULL);
    MF_AddValue(param_menu, "关门行程", &s_state.close_run_ms, 200, 5000, 100, "ms", NULL);

    MF_AddAction(test_menu, "LED测试", Menu_ActionLedTest);
    MF_AddAction(test_menu, "蜂鸣测试", Menu_ActionBuzzerTest);
    MF_AddAction(test_menu, "风扇测试", Menu_ActionFanTest);
    MF_AddAction(test_menu, "闸门测试", Menu_ActionDoorTest);
    MF_AddAction(test_menu, "红外距离", Menu_ActionDistanceRead);
    MF_AddAction(test_menu, "温度采集", Menu_ActionTempRead);
}

void Menu_Init(void)
{
    memset(&s_state, 0, sizeof(s_state));
    memset(&s_buzzer, 0, sizeof(s_buzzer));

    s_state.door_state = DOOR_CLOSED;
    s_state.temp_stage = TEMP_IDLE;
    s_state.temp_deci_c = 250;
    s_state.distance_mm = VL53L0X_DISTANCE_INVALID;
    s_state.temp_threshold_c = 35;
    s_state.auto_close_s = 30;
    s_state.obstacle_threshold_mm = 80;
    s_state.retreat_time_ms = 800;
    s_state.open_run_ms = 1500;
    s_state.close_run_ms = 1500;
    strncpy(s_state.last_command, "NONE", sizeof(s_state.last_command) - 1u);
    strncpy(s_state.last_fault, "NONE", sizeof(s_state.last_fault) - 1u);

    ESP_RegisterCallback(Menu_OnEspPacket);
    Menu_UpdateOutputs();
    Menu_BuildTree();
    MF_Start(s_root_menu);
    MF_Process(KEY_ENTER);
}

void Menu_Task(void)
{
    ESP_Process();
    Menu_ServiceTemperature();
    Menu_ServiceDistance();
    Menu_ServiceDoor();
    Menu_ServiceBuzzer();
    MF_Loop();
}
