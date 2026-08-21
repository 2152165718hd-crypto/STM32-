#include "APPLICATION/Menu/Menu.h"

static AppLockState_t g_lock_state = APP_LOCK_LOCKED;
static AppLastResult_t g_last_result = APP_RESULT_IDLE;
static ESP_ConnectionState_t g_wifi_state = ESP_CONN_INIT_FAIL;

static VerifyState_t g_verify_state = VERIFY_IDLE;
static int16_t g_verify_face_id = -1;
static uint8_t g_verify_error = 0;
static char g_verify_name[FM225_NAME_SIZE + 1] = {0};
static uint8_t g_verify_start_pending = 0;

static EnrollState_t g_enroll_state = ENROLL_IDLE;
static int16_t g_enroll_face_id = -1;
static uint8_t g_enroll_error = 0;
static char g_enroll_name[FM225_NAME_SIZE + 1] = {0};
static uint8_t g_enroll_start_pending = 0;

static uint8_t g_face_count = 0;
static int16_t g_delete_face_id = 1;

static uint8_t g_alarm_active = 0;
static uint8_t g_buzzer_manual_on = 0;
static uint32_t g_alarm_deadline = 0;

static uint8_t g_unlock_pulse_active = 0;
static uint32_t g_unlock_deadline = 0;

static uint32_t g_last_status_push_tick = 0;
static uint8_t g_esp_last_link_id = 0;


static char g_esp_last_rx[APP_ESP_CMD_LEN] = "-";
static char g_esp_last_tx[APP_ESP_TX_LEN] = "-";
static char g_esp_rx_history[APP_ESP_HISTORY_MAX][APP_ESP_CMD_LEN];
static char g_esp_tx_history[APP_ESP_HISTORY_MAX][APP_ESP_TX_LEN];
static uint8_t g_esp_rx_history_count = 0;
static uint8_t g_esp_tx_history_count = 0;

static char g_fmt_buf[APP_ESP_TX_LEN];

static MF_Menu_t *g_menu_main = NULL;

#if APP_USE_IWDG
static IWDG_HandleTypeDef g_hiwdg;
#endif

static void Page_SystemStatus(KeyEvent_t key, uint8_t *exit_flag);
static void Page_FaceVerify(KeyEvent_t key, uint8_t *exit_flag);
static void Page_FaceEnroll(KeyEvent_t key, uint8_t *exit_flag);
static void Page_FaceDelete(KeyEvent_t key, uint8_t *exit_flag);
static void Page_FaceDeleteAll(KeyEvent_t key, uint8_t *exit_flag);
static void Page_FaceDB(KeyEvent_t key, uint8_t *exit_flag);
static void Page_ESPStatus(KeyEvent_t key, uint8_t *exit_flag);
static void Page_ESPRxHistory(KeyEvent_t key, uint8_t *exit_flag);
static void Page_ESPTxHistory(KeyEvent_t key, uint8_t *exit_flag);
static void Page_BuzzerControl(KeyEvent_t key, uint8_t *exit_flag);
static void Page_RelayControl(KeyEvent_t key, uint8_t *exit_flag);

static void App_BuildMenu(void);
static void App_UpdateWifiState(void);
static void App_HandleTimers(void);
static void App_HandlePendingFaceCommand(void);
static uint8_t App_CanStartFaceCommand(void);
static void App_StartVerify(void);
static void App_ResetVerifySession(void);
static void App_ResetEnrollSession(void);
static void App_StartEnrollStep(void);

static void App_UnlockDoorPulse(const char *source);
static void App_UnlockDoorManual(const char *source);
static void App_LockDoor(const char *source, uint8_t emit_event);
static void App_StartAlarm(void);
static void App_ClearAlarm(const char *source, uint8_t emit_event);
static void App_ApplyBuzzerOutput(void);
static void App_PushStatus(void);
static void App_SendMessage(const char *message);
static void App_SendEventFacePass(int16_t face_id, const char *name);
static void App_SendEventFaceFail(void);
static void App_SendEventFaceError(uint8_t error_code);
static void App_SendEventEnrollOk(int16_t face_id, const char *name);
static void App_SendEventLockOpen(const char *source);
static void App_SendEventLockClose(const char *source);
static void App_SendEventAlarmCleared(const char *source);
static void App_RecordRx(const char *message);
static void App_RecordTx(const char *message);
static void App_RecordRxHistory(const char *message);
static void App_RecordTxHistory(const char *message);
static void App_CopyString(char *dst, uint16_t size, const char *src);
static void App_BuildPreview(char *dst, uint16_t size, const char *src, uint8_t max_chars);
static const char *App_GetLockText(void);
static const char *App_GetRelayText(void);
static const char *App_GetBuzzerText(void);

static const char *App_GetWifiText(void);
static const char *App_GetWifiUiText(void);
static const char *App_GetLastResultText(void);
static FM225_FaceDirection_t App_GetEnrollDirection(EnrollState_t state);
static const char *App_GetEnrollPrompt(EnrollState_t state);
static void App_IWDG_Init(void);
static void App_IWDG_Feed(void);

static void OnFaceMatched(int16_t face_id, const char *name)
{
    g_verify_start_pending = 0;
    g_verify_state = VERIFY_SUCCESS;
    g_verify_face_id = face_id;
    g_verify_error = 0;
    App_CopyString(g_verify_name, sizeof(g_verify_name), name);
    g_last_result = APP_RESULT_PASS;

    App_SendEventFacePass(face_id, name);
    App_UnlockDoorPulse("LOCAL");
}

static void OnFaceUnmatched(void)
{
    g_verify_start_pending = 0;
    g_verify_state = VERIFY_FAIL;
    g_verify_face_id = -1;
    g_verify_error = 0;
    App_CopyString(g_verify_name, sizeof(g_verify_name), "-");
    g_last_result = APP_RESULT_FAIL;

    App_StartAlarm();
    App_SendEventFaceFail();
    App_PushStatus();


}

static void OnFaceInvalid(uint8_t error)
{
    g_verify_start_pending = 0;
    g_verify_state = VERIFY_ERROR;
    g_verify_face_id = -1;
    g_verify_error = error;
    App_CopyString(g_verify_name, sizeof(g_verify_name), "-");
    g_last_result = APP_RESULT_ERROR;

    App_StartAlarm();
    App_SendEventFaceError(error);
    App_PushStatus();


}

static void OnEnrollDone(int16_t face_id, uint8_t direction)
{
    (void)direction;
    g_enroll_start_pending = 0;

    if (g_enroll_state < ENROLL_UP)
    {
        g_enroll_state = (EnrollState_t)(g_enroll_state + 1);
        App_StartEnrollStep();
        return;
    }

    g_enroll_face_id = face_id;
    g_enroll_state = ENROLL_DONE;
    FM225_GetFaceCount();
    App_SendEventEnrollOk(face_id, g_enroll_name);
    App_PushStatus();
}

static void OnEnrollFail(uint8_t error)
{
    g_enroll_start_pending = 0;
    g_enroll_error = error;
    g_enroll_state = ENROLL_FAIL;
}

static void OnFaceCount(uint8_t count)
{
    if (count > APP_FACE_MAX_COUNT)
        count = APP_FACE_MAX_COUNT;

    g_face_count = count;

    if (g_face_count == 0)
        g_delete_face_id = 1;
    else if (g_delete_face_id > g_face_count)
        g_delete_face_id = g_face_count;
}

static void OnESPData(ESP_DataPacket_t *packet)
{
    uint16_t len = 0;
    char command[APP_ESP_CMD_LEN];

    if (packet == NULL)
        return;

    len = packet->len;
    if (len >= APP_ESP_CMD_LEN)
        len = APP_ESP_CMD_LEN - 1;

    memcpy(command, packet->data, len);
    command[len] = '\0';

    g_esp_last_link_id = packet->link_id;
    App_RecordRx(command);

    if (strcmp(command, "OPEN") == 0)
    {
        App_UnlockDoorManual("REMOTE");
        return;
    }

    if (strcmp(command, "CLOSE") == 0)
    {
        App_LockDoor("REMOTE", 1);
        return;
    }

    if (strcmp(command, "CLEAR_ALARM") == 0)
    {
        App_ClearAlarm("REMOTE", 1);
        return;
    }

    if (strcmp(command, "GET_STATUS") == 0)
    {
        App_PushStatus();
        return;
    }
}

static void App_CopyString(char *dst, uint16_t size, const char *src)
{
    if (!dst || size == 0)
        return;

    if (!src)
        src = "";

    strncpy(dst, src, size - 1);
    dst[size - 1] = '\0';
}

static void App_RecordRxHistory(const char *message)
{
    uint8_t i;

    if (!message)
        return;

    if (g_esp_rx_history_count < APP_ESP_HISTORY_MAX)
    {
        App_CopyString(g_esp_rx_history[g_esp_rx_history_count], APP_ESP_CMD_LEN, message);
        g_esp_rx_history_count++;
        return;
    }

    for (i = 0; i < (APP_ESP_HISTORY_MAX - 1U); i++)
        App_CopyString(g_esp_rx_history[i], APP_ESP_CMD_LEN, g_esp_rx_history[i + 1U]);

    App_CopyString(g_esp_rx_history[APP_ESP_HISTORY_MAX - 1U], APP_ESP_CMD_LEN, message);
}

static void App_RecordTxHistory(const char *message)
{
    uint8_t i;

    if (!message)
        return;

    if (g_esp_tx_history_count < APP_ESP_HISTORY_MAX)
    {
        App_CopyString(g_esp_tx_history[g_esp_tx_history_count], APP_ESP_TX_LEN, message);
        g_esp_tx_history_count++;
        return;
    }

    for (i = 0; i < (APP_ESP_HISTORY_MAX - 1U); i++)
        App_CopyString(g_esp_tx_history[i], APP_ESP_TX_LEN, g_esp_tx_history[i + 1U]);

    App_CopyString(g_esp_tx_history[APP_ESP_HISTORY_MAX - 1U], APP_ESP_TX_LEN, message);
}

static void App_RecordRx(const char *message)
{
    App_CopyString(g_esp_last_rx, sizeof(g_esp_last_rx), message);
    App_RecordRxHistory(message);
}

static void App_RecordTx(const char *message)
{
    App_CopyString(g_esp_last_tx, sizeof(g_esp_last_tx), message);
    App_RecordTxHistory(message);
}

static void App_BuildPreview(char *dst, uint16_t size, const char *src, uint8_t max_chars)
{
    uint16_t i = 0;
    uint8_t truncated = 0;

    if (!dst || size == 0U)
        return;

    if (!src || src[0] == '\0')
        src = "-";

    if (max_chars >= size)
        max_chars = (uint8_t)(size - 1U);

    while (src[i] != '\0' && i < max_chars)
    {
        dst[i] = src[i];
        i++;
    }

    if (src[i] != '\0')
        truncated = 1U;

    if (truncated && max_chars >= 3U)
    {
        if (i > (uint16_t)(max_chars - 3U))
            i = max_chars - 3U;
        dst[i++] = '.';
        dst[i++] = '.';
        dst[i++] = '.';
    }

    dst[i] = '\0';
}

static void App_SendMessage(const char *message)
{
    if (!message)
        return;

    if (g_wifi_state != ESP_CONN_CLIENT_CONNECTED)
        return;

    if (ESP_SendString(g_esp_last_link_id, message) == ESP_OK)
    {
        App_RecordTx(message);
        g_last_status_push_tick = HAL_GetTick();
    }
}

static void App_SendEventFacePass(int16_t face_id, const char *name)
{
    snprintf(g_fmt_buf, sizeof(g_fmt_buf), "EVENT:FACE_PASS,%d,%s", face_id, name ? name : "-");
    App_SendMessage(g_fmt_buf);
}

static void App_SendEventFaceFail(void)
{
    App_SendMessage("EVENT:FACE_FAIL,0,-");
}

static void App_SendEventFaceError(uint8_t error_code)
{
    snprintf(g_fmt_buf, sizeof(g_fmt_buf), "EVENT:FACE_ERROR,%u,-", error_code);
    App_SendMessage(g_fmt_buf);
}

static void App_SendEventEnrollOk(int16_t face_id, const char *name)
{
    snprintf(g_fmt_buf, sizeof(g_fmt_buf), "EVENT:ENROLL_OK,%d,%s", face_id, name ? name : "-");
    App_SendMessage(g_fmt_buf);
}

static void App_SendEventLockOpen(const char *source)
{
    snprintf(g_fmt_buf, sizeof(g_fmt_buf), "EVENT:LOCK_OPEN,%s,-", source);
    App_SendMessage(g_fmt_buf);
}

static void App_SendEventLockClose(const char *source)
{
    snprintf(g_fmt_buf, sizeof(g_fmt_buf), "EVENT:LOCK_CLOSE,%s,-", source);
    App_SendMessage(g_fmt_buf);
}

static void App_SendEventAlarmCleared(const char *source)
{
    snprintf(g_fmt_buf, sizeof(g_fmt_buf), "EVENT:ALARM_CLEARED,%s,-", source);
    App_SendMessage(g_fmt_buf);
}

static void App_PushStatus(void)
{
    snprintf(g_fmt_buf, sizeof(g_fmt_buf), "STATUS:%s,%s,%s,%s",
             App_GetLockText(),
             App_GetWifiText(),
             g_alarm_active ? "ON" : "OFF",
             App_GetLastResultText());
    App_SendMessage(g_fmt_buf);
}

static const char *App_GetLockText(void)
{
    return (g_lock_state == APP_LOCK_UNLOCKED) ? "UNLOCKED" : "LOCKED";
}

static const char *App_GetRelayText(void)
{
    return (g_lock_state == APP_LOCK_UNLOCKED) ? "ON" : "OFF";
}

static const char *App_GetBuzzerText(void)
{
    return ((g_alarm_active != 0U) || (g_buzzer_manual_on != 0U)) ? "ON" : "OFF";
}


static const char *App_GetWifiText(void)
{
    switch (g_wifi_state)
    {
    case ESP_CONN_CLIENT_CONNECTED:
        return "CLIENT_CONNECTED";
    case ESP_CONN_AP_READY:
        return "AP_READY";
    default:
        return "INIT_FAIL";
    }
}

static const char *App_GetWifiUiText(void)
{
    switch (g_wifi_state)
    {
    case ESP_CONN_CLIENT_CONNECTED:
        return "LINKED";
    case ESP_CONN_AP_READY:
        return "APRDY";
    default:
        return "FAIL";
    }
}

static const char *App_GetLastResultText(void)
{
    switch (g_last_result)
    {
    case APP_RESULT_PASS:
        return "PASS";
    case APP_RESULT_FAIL:
        return "FAIL";
    case APP_RESULT_ERROR:
        return "ERROR";
    default:
        return "IDLE";
    }
}

static uint8_t App_CanStartFaceCommand(void)
{
    if (g_verify_state != VERIFY_IDLE)
        return 0;

    if (g_enroll_state != ENROLL_IDLE)
        return 0;

    if (FM225_GetActiveCommand() != FM225_CMD_NONE)
        return 0;

    return 1;
}

static void App_ResetVerifySession(void)
{
    g_verify_state = VERIFY_IDLE;
    g_verify_face_id = -1;
    g_verify_error = 0;
    g_verify_start_pending = 0;
    App_CopyString(g_verify_name, sizeof(g_verify_name), "");
}

static void App_StartVerify(void)
{
    if (g_verify_state != VERIFY_IDLE || g_enroll_state != ENROLL_IDLE)
        return;

    g_verify_state = VERIFY_WAITING;
    g_verify_face_id = -1;
    g_verify_error = 0;
    g_verify_start_pending = 0;
    App_CopyString(g_verify_name, sizeof(g_verify_name), "");

    if (FM225_GetActiveCommand() == FM225_CMD_NONE)
        FM225_VerifyFace();
    else
        g_verify_start_pending = 1;

    MF_EnterCustomPage(Page_FaceVerify);
}

static void App_ResetEnrollSession(void)
{
    g_enroll_state = ENROLL_IDLE;
    g_enroll_face_id = -1;
    g_enroll_error = 0;
    g_enroll_start_pending = 0;
    App_CopyString(g_enroll_name, sizeof(g_enroll_name), "");
}


static void App_UnlockDoorPulse(const char *source)
{
    Relay_On();
    g_lock_state = APP_LOCK_UNLOCKED;
    g_unlock_pulse_active = 1;
    g_unlock_deadline = HAL_GetTick() + APP_UNLOCK_PULSE_MS;
    App_SendEventLockOpen(source);
    App_PushStatus();
}

static void App_UnlockDoorManual(const char *source)
{
    Relay_On();
    g_lock_state = APP_LOCK_UNLOCKED;
    g_unlock_pulse_active = 0;
    g_unlock_deadline = 0;
    App_SendEventLockOpen(source);
    App_PushStatus();
}

static void App_LockDoor(const char *source, uint8_t emit_event)
{
    Relay_Off();
    g_lock_state = APP_LOCK_LOCKED;
    g_unlock_pulse_active = 0;
    g_unlock_deadline = 0;

    if (emit_event)
        App_SendEventLockClose(source);

    App_PushStatus();
}

static void App_StartAlarm(void)
{
    g_alarm_active = 1;
    g_alarm_deadline = HAL_GetTick() + APP_ALARM_DURATION_MS;
    App_ApplyBuzzerOutput();
}

static void App_ClearAlarm(const char *source, uint8_t emit_event)
{
    g_alarm_active = 0;
    g_alarm_deadline = 0;
    App_ApplyBuzzerOutput();

    if (emit_event)
        App_SendEventAlarmCleared(source);

    App_PushStatus();
}

static void App_ApplyBuzzerOutput(void)
{
    if ((g_alarm_active != 0U) || (g_buzzer_manual_on != 0U))
        Buzzer_On();
    else
        Buzzer_Off();
}

static void App_UpdateWifiState(void)
{
    ESP_ConnectionState_t previous = g_wifi_state;

    g_wifi_state = ESP_GetConnectionState();
    g_esp_last_link_id = ESP_GetActiveLinkId();

    if (g_wifi_state != previous && g_wifi_state == ESP_CONN_CLIENT_CONNECTED)
        App_PushStatus();
}

static void App_HandleTimers(void)
{
    uint32_t now = HAL_GetTick();

    if (g_alarm_active && (int32_t)(now - g_alarm_deadline) >= 0)
    {
        g_alarm_active = 0;
        g_alarm_deadline = 0;
        App_ApplyBuzzerOutput();
        App_PushStatus();
    }

    if (g_unlock_pulse_active && (int32_t)(now - g_unlock_deadline) >= 0)
        App_LockDoor("LOCAL", 1);

    if (g_wifi_state == ESP_CONN_CLIENT_CONNECTED &&
        (now - g_last_status_push_tick) >= APP_STATUS_PUSH_INTERVAL_MS)
        App_PushStatus();
}


static void App_HandlePendingFaceCommand(void)
{
    if (FM225_GetActiveCommand() != FM225_CMD_NONE)
        return;

    if (g_verify_start_pending && g_verify_state == VERIFY_WAITING)
    {
        g_verify_start_pending = 0;
        FM225_VerifyFace();
        return;
    }

    if (g_enroll_start_pending &&
        g_enroll_state >= ENROLL_MIDDLE &&
        g_enroll_state <= ENROLL_UP)
    {
        g_enroll_start_pending = 0;
        App_StartEnrollStep();
    }
}


static FM225_FaceDirection_t App_GetEnrollDirection(EnrollState_t state)
{
    switch (state)
    {
    case ENROLL_RIGHT:
        return FM225_DIR_RIGHT;
    case ENROLL_LEFT:
        return FM225_DIR_LEFT;
    case ENROLL_DOWN:
        return FM225_DIR_DOWN;
    case ENROLL_UP:
        return FM225_DIR_UP;
    case ENROLL_MIDDLE:
    default:
        return FM225_DIR_MIDDLE;
    }
}

static const char *App_GetEnrollPrompt(EnrollState_t state)
{
    switch (state)
    {
    case ENROLL_RIGHT:
        return "Turn right >";
    case ENROLL_LEFT:
        return "Turn left <";
    case ENROLL_DOWN:
        return "Look down v";
    case ENROLL_UP:
        return "Look up ^";
    case ENROLL_MIDDLE:
    default:
        return "Face camera";
    }
}

static void App_StartEnrollStep(void)
{
    FM225_FaceDirection_t direction;

    if (FM225_GetActiveCommand() != FM225_CMD_NONE)
        return;

    direction = App_GetEnrollDirection(g_enroll_state);
    FM225_EnrollFace(g_enroll_name, direction);
}

static void Page_SystemStatus(KeyEvent_t key, uint8_t *exit_flag)
{
    if (key == KEY_BACK)
    {
        *exit_flag = 1;
        return;
    }

    OLED_ShowString(0, 0, "System Info", OLED_8X16);
    OLED_Printf(0, 16, OLED_8X16, "Lock:%s", App_GetLockText());
    OLED_Printf(0, 32, OLED_8X16, "WiFi:%s", App_GetWifiUiText());
    OLED_Printf(0, 48, OLED_8X16, "Last:%s", App_GetLastResultText());
}

static void Page_FaceVerify(KeyEvent_t key, uint8_t *exit_flag)
{
    char preview[16];

    if (g_verify_state == VERIFY_IDLE && key == KEY_BACK)
    {
        *exit_flag = 1;
        return;
    }

    if (g_verify_start_pending && key == KEY_BACK)
    {
        App_ResetVerifySession();
        *exit_flag = 1;
        return;
    }

    /* 正在识别中按 BACK：中止模块操作并退出 */
    if (g_verify_state == VERIFY_WAITING && !g_verify_start_pending && key == KEY_BACK)
    {
        FM225_Abort();
        App_ResetVerifySession();
        *exit_flag = 1;
        return;
    }

    if ((g_verify_state == VERIFY_SUCCESS || g_verify_state == VERIFY_FAIL || g_verify_state == VERIFY_ERROR) &&
        (key == KEY_ENTER || key == KEY_BACK))
    {
        App_ResetVerifySession();
        *exit_flag = 1;
        return;
    }

    OLED_ShowString(0, 0, "Face Verify", OLED_8X16);

    switch (g_verify_state)
    {
    case VERIFY_IDLE:
        OLED_ShowString(8, 16, "Press OK", OLED_8X16);
        OLED_ShowString(8, 32, "To Verify", OLED_8X16);
        OLED_ShowString(0, 48, "BACK Return", OLED_8X16);
        if (key == KEY_ENTER)
            App_StartVerify();
        break;

    case VERIFY_WAITING:
        if (g_verify_start_pending)
        {
            OLED_ShowString(0, 16, "Module Busy", OLED_8X16);
            OLED_ShowString(0, 32, "Please Wait", OLED_8X16);
            OLED_ShowString(0, 48, "Wait/BACK", OLED_8X16);
        }
        else
        {
            OLED_ShowString(0, 16, "Verifying...", OLED_8X16);
            OLED_ShowString(8, 32, "Keep Still", OLED_8X16);
            OLED_ShowString(0, 48, "BACK:Cancel", OLED_8X16);
        }
        break;

    case VERIFY_SUCCESS:
        App_BuildPreview(preview, sizeof(preview), g_verify_name, 15U);
        OLED_Printf(0, 16, OLED_8X16, "PASS ID:%d", g_verify_face_id);
        OLED_ShowString(0, 32, preview, OLED_8X16);
        OLED_Printf(0, 48, OLED_8X16, "Relay:%s", App_GetRelayText());
        break;

    case VERIFY_FAIL:
        OLED_ShowString(16, 16, "No Match", OLED_8X16);
        OLED_ShowString(16, 32, "Alarm ON", OLED_8X16);
        OLED_ShowString(0, 48, "OK/BACK Exit", OLED_8X16);
        break;

    case VERIFY_ERROR:
        OLED_ShowString(24, 16, "Error", OLED_8X16);
        OLED_Printf(0, 32, OLED_8X16, "Code:0x%02X", g_verify_error);
        OLED_ShowString(0, 48, "OK/BACK Exit", OLED_8X16);
        break;
    }
}

static void Page_FaceEnroll(KeyEvent_t key, uint8_t *exit_flag)
{
    if (g_enroll_start_pending && key == KEY_BACK)
    {
        App_ResetEnrollSession();
        *exit_flag = 1;
        return;
    }

    /* 正在录入中按 BACK：中止模块操作并退出 */
    if (g_enroll_state >= ENROLL_MIDDLE && g_enroll_state <= ENROLL_UP &&
        !g_enroll_start_pending && key == KEY_BACK)
    {
        FM225_Abort();
        App_ResetEnrollSession();
        *exit_flag = 1;
        return;
    }

    if ((g_enroll_state == ENROLL_IDLE || g_enroll_state == ENROLL_DONE || g_enroll_state == ENROLL_FAIL) &&
        key == KEY_BACK)
    {
        if (g_enroll_state != ENROLL_IDLE)
            App_ResetEnrollSession();
        *exit_flag = 1;
        return;
    }

    OLED_ShowString(0, 0, "Face Enroll", OLED_8X16);

    switch (g_enroll_state)
    {
    case ENROLL_IDLE:
        if (g_face_count >= APP_FACE_MAX_COUNT)
        {
            OLED_ShowString(0, 16, "Face DB Full", OLED_8X16);
            OLED_ShowString(8, 32, "Delete First", OLED_8X16);
            OLED_ShowString(0, 48, "BACK Return", OLED_8X16);
            break;
        }

        OLED_ShowString(8, 16, "Press OK", OLED_8X16);
        OLED_ShowString(8, 32, "To Enroll", OLED_8X16);
        OLED_ShowString(0, 48, "BACK Return", OLED_8X16);
        if (key == KEY_ENTER)
        {
            g_enroll_state = ENROLL_MIDDLE;
            g_enroll_face_id = -1;
            g_enroll_error = 0;
            g_enroll_start_pending = 0;
            snprintf(g_enroll_name, sizeof(g_enroll_name), "User%u", (unsigned int)(g_face_count + 1U));
            if (FM225_GetActiveCommand() == FM225_CMD_NONE)
                App_StartEnrollStep();
            else
                g_enroll_start_pending = 1;
        }
        break;

    case ENROLL_MIDDLE:
    case ENROLL_RIGHT:
    case ENROLL_LEFT:
    case ENROLL_DOWN:
    case ENROLL_UP:
        if (g_enroll_start_pending)
        {
            OLED_ShowString(0, 16, "Module Busy", OLED_8X16);
            OLED_ShowString(0, 32, (char *)App_GetEnrollPrompt(g_enroll_state), OLED_8X16);
            OLED_ShowString(0, 48, "Wait/BACK", OLED_8X16);
        }
        else
        {
            OLED_Printf(8, 16, OLED_8X16, "Step %d/5", (int)(g_enroll_state - ENROLL_MIDDLE + 1));
            OLED_ShowString(0, 32, (char *)App_GetEnrollPrompt(g_enroll_state), OLED_8X16);
            OLED_ShowString(0, 48, "BACK:Cancel", OLED_8X16);
        }
        break;

    case ENROLL_DONE:
        OLED_ShowString(8, 16, "Enroll OK", OLED_8X16);
        OLED_Printf(16, 32, OLED_8X16, "ID:%d", g_enroll_face_id);
        OLED_ShowString(0, 48, "OK/BACK Exit", OLED_8X16);
        if (key == KEY_ENTER)
        {
            App_ResetEnrollSession();
            *exit_flag = 1;
        }
        break;

    case ENROLL_FAIL:
        OLED_ShowString(0, 16, "Enroll Fail", OLED_8X16);
        OLED_Printf(0, 32, OLED_8X16, "Err:0x%02X", g_enroll_error);
        OLED_ShowString(0, 48, "OK Retry", OLED_8X16);
        if (key == KEY_ENTER)
            App_ResetEnrollSession();
        break;
    }
}

static void Page_FaceDelete(KeyEvent_t key, uint8_t *exit_flag)
{
    static uint8_t confirm_delete = 0;
    static uint8_t delete_done = 0;

    if (key == KEY_BACK && !confirm_delete && !delete_done)
    {
        confirm_delete = 0;
        delete_done = 0;
        *exit_flag = 1;
        return;
    }

    OLED_ShowString(0, 0, "Delete One", OLED_8X16);

    if (delete_done)
    {
        OLED_ShowString(0, 16, "Delete Sent", OLED_8X16);
        OLED_ShowString(8, 32, "Refresh DB", OLED_8X16);
        OLED_ShowString(0, 48, "OK/BACK Exit", OLED_8X16);
        if (key == KEY_ENTER || key == KEY_BACK)
        {
            delete_done = 0;
            FM225_GetFaceCount();
            *exit_flag = 1;
        }
        return;
    }

    if (g_face_count == 0)
    {
        OLED_ShowString(16, 16, "No Faces", OLED_8X16);
        OLED_ShowString(24, 32, "DB Empty", OLED_8X16);
        OLED_ShowString(0, 48, "BACK Return", OLED_8X16);
        return;
    }

    if (g_delete_face_id > g_face_count)
        g_delete_face_id = g_face_count;
    if (g_delete_face_id < 1)
        g_delete_face_id = 1;

    if (confirm_delete)
    {
        OLED_Printf(0, 16, OLED_8X16, "Delete ID:%d?", g_delete_face_id);
        OLED_ShowString(8, 32, "OK Confirm", OLED_8X16);
        OLED_ShowString(0, 48, "BACK Cancel", OLED_8X16);

        if (key == KEY_ENTER && FM225_GetActiveCommand() == FM225_CMD_NONE)
        {
            FM225_DeleteFace(g_delete_face_id);
            delete_done = 1;
            confirm_delete = 0;
        }
        else if (key == KEY_BACK)
        {
            confirm_delete = 0;
        }
        return;
    }

    if (key == KEY_UP)
    {
        g_delete_face_id++;
        if (g_delete_face_id > g_face_count)
            g_delete_face_id = 1;
    }
    else if (key == KEY_DOWN)
    {
        g_delete_face_id--;
        if (g_delete_face_id < 1)
            g_delete_face_id = g_face_count;
    }
    else if (key == KEY_ENTER)
    {
        confirm_delete = 1;
    }

    OLED_Printf(16, 16, OLED_8X16, "ID:%d/%u", g_delete_face_id, (unsigned int)g_face_count);
    OLED_ShowString(8, 32, "UP/DN Select", OLED_8X16);
    OLED_ShowString(16, 48, "OK Delete", OLED_8X16);
}

static void Page_FaceDeleteAll(KeyEvent_t key, uint8_t *exit_flag)
{
    static uint8_t delete_done = 0;

    if (key == KEY_BACK && !delete_done)
    {
        *exit_flag = 1;
        return;
    }

    OLED_ShowString(0, 0, "Delete All", OLED_8X16);

    if (delete_done)
    {
        OLED_ShowString(0, 16, "Delete Sent", OLED_8X16);
        OLED_ShowString(8, 32, "DB Cleared", OLED_8X16);
        OLED_ShowString(0, 48, "OK/BACK Exit", OLED_8X16);
        if (key == KEY_ENTER || key == KEY_BACK)
        {
            delete_done = 0;
            *exit_flag = 1;
        }
        return;
    }

    if (g_face_count == 0)
    {
        OLED_ShowString(8, 16, "Database", OLED_8X16);
        OLED_ShowString(24, 32, "Empty", OLED_8X16);
        OLED_ShowString(0, 48, "BACK Return", OLED_8X16);
        return;
    }

    if (key == KEY_ENTER && FM225_GetActiveCommand() == FM225_CMD_NONE)
    {
        FM225_DeleteAllFaces();
        g_face_count = 0;
        g_delete_face_id = 1;
        delete_done = 1;
    }

    OLED_Printf(16, 16, OLED_8X16, "Faces:%u", (unsigned int)g_face_count);
    OLED_ShowString(16, 32, "OK Del All", OLED_8X16);
    OLED_ShowString(0, 48, "BACK Cancel", OLED_8X16);
}

static void Page_FaceDB(KeyEvent_t key, uint8_t *exit_flag)
{
    if (key == KEY_BACK)
    {
        *exit_flag = 1;
        return;
    }

    if (key == KEY_ENTER)
        FM225_GetFaceCount();

    OLED_ShowString(0, 0, "Face DB Info", OLED_8X16);
    OLED_Printf(8, 16, OLED_8X16, "Faces:%u", (unsigned int)g_face_count);
    if (g_face_count >= APP_FACE_MAX_COUNT)
        OLED_ShowString(0, 32, "NextID:FULL", OLED_8X16);
    else
        OLED_Printf(0, 32, OLED_8X16, "NextID:%u", (unsigned int)(g_face_count + 1U));
    OLED_Printf(0, 48, OLED_8X16, "Busy:%s OK:R",
                (FM225_GetActiveCommand() == FM225_CMD_NONE) ? "NO" : "YES");
}

static void Page_ESPStatus(KeyEvent_t key, uint8_t *exit_flag)
{
    if (key == KEY_BACK)
    {
        *exit_flag = 1;
        return;
    }

    OLED_ShowString(0, 0, "ESP Status", OLED_8X16);
    OLED_Printf(0, 16, OLED_8X16, "WiFi:%s", App_GetWifiUiText());
    OLED_Printf(8, 32, OLED_8X16, "Client:%u", (unsigned int)ESP_GetClientCount());
    OLED_Printf(0, 48, OLED_8X16, "Port:%s Link:%u",
                ESP_SERVER_PORT,
                (unsigned int)g_esp_last_link_id);
}

static void Page_ESPRxHistory(KeyEvent_t key, uint8_t *exit_flag)
{
    uint8_t i;

    if (key == KEY_BACK)
    {
        *exit_flag = 1;
        return;
    }

    OLED_ShowString(0, 0, "RX History", OLED_8X16);

    if (g_esp_rx_history_count == 0)
    {
        OLED_ShowString(8, 16, "No RX Data", OLED_8X16);
        OLED_ShowString(0, 48, "BACK Return", OLED_8X16);
        return;
    }

    for (i = 0; i < g_esp_rx_history_count; i++)
    {
        char preview[APP_OLED_PREVIEW_LEN + 1U];
        uint8_t idx = (uint8_t)(g_esp_rx_history_count - 1U - i);
        App_BuildPreview(preview, sizeof(preview), g_esp_rx_history[idx], APP_OLED_PREVIEW_LEN);
        OLED_Printf(0, 16 + (i * 8), OLED_6X8, "%u:%s", (unsigned int)(i + 1U), preview);
    }
}

static void Page_ESPTxHistory(KeyEvent_t key, uint8_t *exit_flag)
{
    uint8_t i;

    if (key == KEY_BACK)
    {
        *exit_flag = 1;
        return;
    }

    OLED_ShowString(0, 0, "TX History", OLED_8X16);

    if (g_esp_tx_history_count == 0)
    {
        OLED_ShowString(8, 16, "No TX Data", OLED_8X16);
        OLED_ShowString(0, 48, "BACK Return", OLED_8X16);
        return;
    }

    for (i = 0; i < g_esp_tx_history_count; i++)
    {
        char preview[APP_OLED_PREVIEW_LEN + 1U];
        uint8_t idx = (uint8_t)(g_esp_tx_history_count - 1U - i);
        App_BuildPreview(preview, sizeof(preview), g_esp_tx_history[idx], APP_OLED_PREVIEW_LEN);
        OLED_Printf(0, 16 + (i * 8), OLED_6X8, "%u:%s", (unsigned int)(i + 1U), preview);
    }
}

static void Page_BuzzerControl(KeyEvent_t key, uint8_t *exit_flag)
{
    if (key == KEY_BACK)
    {
        *exit_flag = 1;
        return;
    }

    if (key == KEY_UP)
    {
        g_buzzer_manual_on = 1;
        App_ApplyBuzzerOutput();
    }
    else if (key == KEY_DOWN)
    {
        g_buzzer_manual_on = 0;
        App_ApplyBuzzerOutput();
    }
    else if (key == KEY_ENTER)
    {
        g_buzzer_manual_on ^= 1U;
        App_ApplyBuzzerOutput();
    }

    OLED_ShowString(0, 0, "Buzzer Ctrl", OLED_8X16);
    OLED_Printf(8, 16, OLED_8X16, "Buzz:%s A:%s",
                App_GetBuzzerText(),
                g_alarm_active ? "ON" : "OFF");
    OLED_Printf(8, 32, OLED_8X16, "Manual:%s", g_buzzer_manual_on ? "ON" : "OFF");
    OLED_ShowString(8, 48, "UP/DN OK Ctrl", OLED_8X16);
}

static void Page_RelayControl(KeyEvent_t key, uint8_t *exit_flag)
{
    if (key == KEY_BACK)
    {
        *exit_flag = 1;
        return;
    }

    if (key == KEY_UP)
    {
        App_UnlockDoorManual("LOCAL");
    }
    else if (key == KEY_DOWN)
    {
        App_LockDoor("LOCAL", 1);
    }
    else if (key == KEY_ENTER)
    {
        if (g_lock_state == APP_LOCK_UNLOCKED || g_unlock_pulse_active)
            App_LockDoor("LOCAL", 1);
        else
            App_UnlockDoorManual("LOCAL");
    }

    OLED_ShowString(0, 0, "Relay Ctrl", OLED_8X16);
    OLED_Printf(0, 16, OLED_8X16, "Relay:%s P:%s",
                App_GetRelayText(),
                g_unlock_pulse_active ? "ON" : "OFF");
    OLED_Printf(0, 32, OLED_8X16, "Lock:%s", App_GetLockText());
    OLED_ShowString(8, 48, "UP/DN OK Ctrl", OLED_8X16);
}

static void App_BuildMenu(void)
{
    MF_Menu_t *menu_face;
    MF_Menu_t *menu_face_delete;
    MF_Menu_t *menu_actuator;
    MF_Menu_t *menu_esp;

    MF_Reset();

    g_menu_main = MF_CreateMenu("Main Menu");
    menu_face = MF_CreateMenu("Face Manage");
    menu_face_delete = MF_CreateMenu("Delete Face");
    menu_actuator = MF_CreateMenu("Actuator Ctrl");
    menu_esp = MF_CreateMenu("ESP Debug");

    MF_AddCustomPage(g_menu_main, "System Info", Page_SystemStatus);
    MF_AddSubmenu(g_menu_main, "Face Manage", menu_face);
    MF_AddSubmenu(g_menu_main, "ESP Debug", menu_esp);
    MF_AddSubmenu(g_menu_main, "Actuator Ctrl", menu_actuator);

    MF_AddCustomPage(menu_face, "Face Verify", Page_FaceVerify);
    MF_AddCustomPage(menu_face, "Face Enroll", Page_FaceEnroll);
    MF_AddSubmenu(menu_face, "Delete Face", menu_face_delete);
    MF_AddCustomPage(menu_face, "Face DB Info", Page_FaceDB);

    MF_AddCustomPage(menu_face_delete, "Delete One", Page_FaceDelete);
    MF_AddCustomPage(menu_face_delete, "Delete All", Page_FaceDeleteAll);

    MF_AddCustomPage(menu_esp, "ESP Status", Page_ESPStatus);
    MF_AddCustomPage(menu_esp, "RX History", Page_ESPRxHistory);
    MF_AddCustomPage(menu_esp, "TX History", Page_ESPTxHistory);

    MF_AddCustomPage(menu_actuator, "Buzzer Ctrl", Page_BuzzerControl);
    MF_AddCustomPage(menu_actuator, "Relay Ctrl", Page_RelayControl);

    MF_SetCursorStyle(MF_CURSOR_BOX);
    MF_Start(g_menu_main);
}

static void App_IWDG_Init(void)
{
#if APP_USE_IWDG
    g_hiwdg.Instance = IWDG;
    g_hiwdg.Init.Prescaler = IWDG_PRESCALER_256;
    g_hiwdg.Init.Reload = 1250U;
    if (HAL_IWDG_Init(&g_hiwdg) != HAL_OK)
    {
        while (1)
        {
        }
    }
#endif
}

static void App_IWDG_Feed(void)
{
#if APP_USE_IWDG
    (void)HAL_IWDG_Refresh(&g_hiwdg);
#endif
}

void App_Init(void)
{
    ESP_Status_t esp_init_result;

    HAL_Init();
    sys_stm32_clock_init(RCC_PLL_MUL9);
    delay_init(72);

    OLED_Init();
    Key_Init();
    Buzzer_Init();
    Relay_Init();
    FM225_Init();
    esp_init_result = ESP_Init();

    Relay_Off();
    Buzzer_Off();

    FM225_SetMatchedCallback(OnFaceMatched);
    FM225_SetUnmatchedCallback(OnFaceUnmatched);
    FM225_SetInvalidCallback(OnFaceInvalid);
    FM225_SetEnrollDoneCallback(OnEnrollDone);
    FM225_SetEnrollFailCallback(OnEnrollFail);
    FM225_SetFaceCountCallback(OnFaceCount);
    ESP_RegisterCallback(OnESPData);

    g_wifi_state = (esp_init_result == ESP_OK) ? ESP_CONN_AP_READY : ESP_CONN_INIT_FAIL;
    g_esp_last_link_id = ESP_GetActiveLinkId();

    App_BuildMenu();

    OLED_Clear();
    OLED_ShowString(20, 8, "System Init", OLED_8X16);
    OLED_ShowString(8, 28, "Face / ESP / IO", OLED_8X16);
    OLED_ShowString(12, 48, "Loading menu...", OLED_8X16);
    OLED_Update();
    HAL_Delay(1000);

    App_IWDG_Init();
    App_IWDG_Feed();

    MF_EnterCustomPage(Page_SystemStatus);
    FM225_GetFaceCount();
}

void App_Loop(void)
{
    FM225_Process();
    ESP_Process();

    App_UpdateWifiState();
    App_HandleTimers();
    App_HandlePendingFaceCommand();

    MF_Loop();

    App_IWDG_Feed();
}
