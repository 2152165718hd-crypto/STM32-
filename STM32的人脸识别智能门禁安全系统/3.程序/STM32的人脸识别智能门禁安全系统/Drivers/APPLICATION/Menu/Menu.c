#include ".\Application\Menu\Menu.h"
#include <stdio.h>

/* ====================================================================
 * STM32 人脸识别智能门禁系统 - 应用层实现
 * 基于 MenuFramework 菜单框架与各功能驱动模块
 * ==================================================================== */

/* 全局状态变量 */
static uint16_t g_light_value = 0;
static uint8_t g_ir_status = 0;
static int32_t g_light_threshold = APP_LIGHT_THRESHOLD_DEFAULT;
static uint8_t g_filllight_locked = 0;
static uint8_t g_ir_prev = 0;
static uint8_t g_ir_manual_mask = 0; /* 红外手动屏蔽：1=开启，0=关闭 */

static VerifyState_t g_verify_state = VERIFY_IDLE;
static int16_t g_verify_face_id = -1;
static char g_verify_name[FM225_NAME_SIZE + 1] = {0};

static EnrollState_t g_enroll_state = ENROLL_IDLE;
static int16_t g_enroll_face_id = -1;
static uint8_t g_enroll_err = 0;

static uint8_t g_face_count = 0;

static uint8_t g_rfid_state = 0;
static RC522_CardInfo_t g_rfid_card;
static uint8_t g_rfid_auto_detected = 0;

static char g_esp_history[APP_ESP_HISTORY_MAX][APP_ESP_CMD_LEN];
static uint8_t g_esp_history_count = 0;

static uint32_t g_esp_upload_tick = 0;

static uint8_t g_door_open = 0;
static uint32_t g_door_open_tick = 0;
static uint8_t g_buzzer_err = 0;
static uint32_t g_buzzer_err_tick = 0;
static uint8_t g_feedback_active = 0;
static uint32_t g_feedback_tick = 0;

static int16_t g_del_face_id = 1;

static uint8_t g_del_card_idx = 0;

static const char *cursor_names[] = {"Reverse", "Box"};

static char fmt_buf[40];

#if APP_USE_IWDG
static IWDG_HandleTypeDef hiwdg_app;
#endif

/* 自定义页面回调声明 */
static void Page_LightSensor(KeyEvent_t key, uint8_t *exit_flag);
static void Page_LightThreshold(KeyEvent_t key, uint8_t *exit_flag);
static void Page_IRSensor(KeyEvent_t key, uint8_t *exit_flag);
static void Page_IRShield(KeyEvent_t key, uint8_t *exit_flag);
static void Page_FaceVerify(KeyEvent_t key, uint8_t *exit_flag);
static void Page_FaceEnroll(KeyEvent_t key, uint8_t *exit_flag);
static void Page_FaceDelete(KeyEvent_t key, uint8_t *exit_flag);
static void Page_FaceDB(KeyEvent_t key, uint8_t *exit_flag);
static void Page_RFIDVerify(KeyEvent_t key, uint8_t *exit_flag);
static void Page_RFIDEnroll(KeyEvent_t key, uint8_t *exit_flag);
static void Page_RFIDDelete(KeyEvent_t key, uint8_t *exit_flag);
static void Page_ESPHistory(KeyEvent_t key, uint8_t *exit_flag);

/* 菜单动作回调声明 */
static void Action_DeleteAllFaces(void);
static void Action_DeleteAllCards(void);
static void Action_ToggleCursor(void);

/* 内部辅助函数声明 */
static void App_DoorOpen(void);
static void App_AccessGranted(int16_t id, const char *name);
static void App_AccessDenied(void);
static void App_FeedbackUpdate(void);
static void App_AutoLightControl(void);
static void App_AutoIRTrigger(void);
static void App_AutoStatusUpload(void);
static void App_ESPCommand(const char *cmd);
static void Menu_BuildTree(void);
static void App_EnrollSuccessFeedback(void);
static void App_IWDG_Init(void);
static void App_IWDG_Feed(void);

static FM225_FaceDirection_t Enroll_GetDirection(EnrollState_t state);
static const char *Enroll_GetPrompt(EnrollState_t state);
static void Enroll_StartNextStep(void);

/* FM225 回调实现 */
static void OnFaceMatched(int16_t face_id, const char *name)
{
    g_verify_state = VERIFY_SUCCESS;
    g_verify_face_id = face_id;
    memset(g_verify_name, 0, sizeof(g_verify_name));
    strncpy(g_verify_name, name, FM225_NAME_SIZE);

    App_AccessGranted(face_id, name);
}

static void OnFaceUnmatched(void)
{
    g_verify_state = VERIFY_FAIL;
    App_AccessDenied();
}

static void OnFaceInvalid(uint8_t error)
{
    g_verify_state = VERIFY_ERROR;
    g_enroll_err = error;

    App_AccessDenied();
}

static void OnEnrollDone(int16_t face_id, uint8_t direction)
{
    if (face_id == -1)
    {
        if (g_enroll_state < ENROLL_UP)
        {
            g_enroll_state++;
            Enroll_StartNextStep();
        }
    }
    else
    {
        g_enroll_face_id = face_id;
        g_enroll_state = ENROLL_DONE;
        FM225_GetFaceCount();
        App_EnrollSuccessFeedback();
    }
}

static void OnEnrollFail(uint8_t error)
{
    g_enroll_err = error;
    g_enroll_state = ENROLL_FAIL;
}

static void OnFaceCount(uint8_t count)
{
    g_face_count = count;
}

/* ESP 数据回调实现 */
static void OnESPData(ESP_DataPacket_t *packet)
{
    char cmd[APP_ESP_CMD_LEN];
    uint16_t len = packet->len;

    if (len >= APP_ESP_CMD_LEN)
        len = APP_ESP_CMD_LEN - 1;

    memcpy(cmd, packet->data, len);
    cmd[len] = '\0';

    if (g_esp_history_count < APP_ESP_HISTORY_MAX)
    {
        strncpy(g_esp_history[g_esp_history_count], cmd, APP_ESP_CMD_LEN - 1);
        g_esp_history[g_esp_history_count][APP_ESP_CMD_LEN - 1] = '\0';
        g_esp_history_count++;
    }
    else
    {
        uint8_t i;
        for (i = 0; i < APP_ESP_HISTORY_MAX - 1; i++)
            memcpy(g_esp_history[i], g_esp_history[i + 1], APP_ESP_CMD_LEN);
        strncpy(g_esp_history[APP_ESP_HISTORY_MAX - 1], cmd, APP_ESP_CMD_LEN - 1);
        g_esp_history[APP_ESP_HISTORY_MAX - 1][APP_ESP_CMD_LEN - 1] = '\0';
    }

    App_ESPCommand(cmd);
}

static void App_DoorOpen(void)
{
    Servo_SetAngle(APP_DOOR_OPEN_ANGLE);
    g_door_open = 1;
    g_door_open_tick = HAL_GetTick();
}

static void App_EnrollSuccessFeedback(void)
{
    LED_On(GREEN_LED_GPIO_PORT, GREEN_LED_PIN);
    LED_Off(RED_LED_GPIO_PORT, RED_LED_PIN);

    Buzzer_On();
    HAL_Delay(150);
    Buzzer_Off();

    g_feedback_active = 1;
    g_feedback_tick = HAL_GetTick();
}

static void App_AccessGranted(int16_t id, const char *name)
{
    LED_On(GREEN_LED_GPIO_PORT, GREEN_LED_PIN);
    LED_Off(RED_LED_GPIO_PORT, RED_LED_PIN);

    Buzzer_On();
    HAL_Delay(150);
    Buzzer_Off();

    App_DoorOpen();

    g_feedback_active = 1;
    g_feedback_tick = HAL_GetTick();

    snprintf(fmt_buf, sizeof(fmt_buf), "PASS:ID%d", id);
    ESP_SendString(0, fmt_buf);
}

static void App_AccessDenied(void)
{
    LED_On(RED_LED_GPIO_PORT, RED_LED_PIN);
    LED_Off(GREEN_LED_GPIO_PORT, GREEN_LED_PIN);

    g_buzzer_err = 1;
    g_buzzer_err_tick = HAL_GetTick();
    Buzzer_On();

    g_feedback_active = 1;
    g_feedback_tick = HAL_GetTick();

    ESP_SendString(0, "DENIED");
}

static void App_FeedbackUpdate(void)
{
    uint32_t now = HAL_GetTick();

    if (g_buzzer_err)
    {
        uint32_t elapsed = now - g_buzzer_err_tick;
        if (elapsed >= APP_BUZZER_ERR_TIME)
        {
            Buzzer_Off();
            g_buzzer_err = 0;
        }
        else
        {
            uint32_t phase = (elapsed / APP_BUZZER_ERR_PERIOD) % 2;
            if (phase == 0)
                Buzzer_On();
            else
                Buzzer_Off();
        }
    }

    if (g_door_open)
    {
        if (now - g_door_open_tick >= APP_DOOR_OPEN_TIME)
        {
            Servo_SetAngle(APP_DOOR_CLOSE_ANGLE);
            g_door_open = 0;
        }
    }

    if (g_feedback_active)
    {
        if (now - g_feedback_tick >= APP_DOOR_OPEN_TIME + 500)
        {
            LED_Off(RED_LED_GPIO_PORT, RED_LED_PIN);
            LED_Off(GREEN_LED_GPIO_PORT, GREEN_LED_PIN);
            g_feedback_active = 0;
        }
    }
}

static void App_AutoLightControl(void)
{
    if (g_filllight_locked)
        return;

    LED_Off(FILLLIGHT_LED_GPIO_PORT, FILLLIGHT_LED_PIN);
}

static void FillLight_LockIfNeeded(void)
{
    if (g_light_value < (uint16_t)g_light_threshold)
    {
        LED_On(FILLLIGHT_LED_GPIO_PORT, FILLLIGHT_LED_PIN);
        g_filllight_locked = 1;
    }
}

static void FillLight_Unlock(void)
{
    if (g_filllight_locked)
    {
        LED_Off(FILLLIGHT_LED_GPIO_PORT, FILLLIGHT_LED_PIN);
        g_filllight_locked = 0;
    }
}

static void App_AutoIRTrigger(void)
{
    if (g_ir_manual_mask || SR505_IsWarmupMasking())
    {
        return;
    }

    if (g_ir_status && !g_ir_prev)
    {
        if (g_verify_state == VERIFY_IDLE && g_enroll_state == ENROLL_IDLE && MF_IsMainMenu())
        {
            FillLight_LockIfNeeded();
            g_verify_state = VERIFY_WAITING;
            FM225_VerifyFace();
            MF_EnterCustomPage(Page_FaceVerify);
        }
    }
}

static void App_AutoStatusUpload(void)
{
    uint32_t now = HAL_GetTick();
    if ((now - g_esp_upload_tick) < APP_ESP_UPLOAD_INTERVAL)
        return;
    g_esp_upload_tick = now;

    /* 状态上报格式：STATUS:<door>,<light>,<ir>
     * door  = 1(开门) / 0(关门)
     * light = 光照 ADC 值，范围 0~4095
     * ir    = 1(有人) / 0(无人)
     */
    snprintf(fmt_buf, sizeof(fmt_buf), "STATUS:%d,%d,%d",
             g_door_open, g_light_value, g_ir_status);
    ESP_SendString(0, fmt_buf);
}

static void App_ESPCommand(const char *cmd)
{
    if (strncmp(cmd, "OPEN", 4) == 0)
    {
        App_AccessGranted(0, "Remote");
    }
    else if (strncmp(cmd, "CLOSE", 5) == 0)
    {
        Servo_SetAngle(APP_DOOR_CLOSE_ANGLE);
        g_door_open = 0;
        ESP_SendString(0, "DOOR:CLOSED");
    }
    else if (strncmp(cmd, "ALARM", 5) == 0)
    {
        LED_On(RED_LED_GPIO_PORT, RED_LED_PIN);
        LED_Off(GREEN_LED_GPIO_PORT, GREEN_LED_PIN);
        g_buzzer_err = 1;
        g_buzzer_err_tick = HAL_GetTick();
        Buzzer_On();
        g_feedback_active = 1;
        g_feedback_tick = HAL_GetTick();
        ESP_SendString(0, "ALARM:ACK");
    }
}

static FM225_FaceDirection_t Enroll_GetDirection(EnrollState_t state)
{
    switch (state)
    {
    case ENROLL_MIDDLE:
        return FM225_DIR_MIDDLE;
    case ENROLL_RIGHT:
        return FM225_DIR_RIGHT;
    case ENROLL_LEFT:
        return FM225_DIR_LEFT;
    case ENROLL_DOWN:
        return FM225_DIR_DOWN;
    case ENROLL_UP:
        return FM225_DIR_UP;
    default:
        return FM225_DIR_MIDDLE;
    }
}

static const char *Enroll_GetPrompt(EnrollState_t state)
{
    switch (state)
    {
    case ENROLL_MIDDLE:
        return "Please face camera";
    case ENROLL_RIGHT:
        return "Turn right ->";
    case ENROLL_LEFT:
        return "<- Turn left";
    case ENROLL_DOWN:
        return "Look down v";
    case ENROLL_UP:
        return "Look up ^";
    case ENROLL_DONE:
        return "Enroll success!";
    case ENROLL_FAIL:
        return "Enroll failed!";
    default:
        return "Ready";
    }
}

static void Enroll_StartNextStep(void)
{
    FM225_FaceDirection_t dir = Enroll_GetDirection(g_enroll_state);
    snprintf(fmt_buf, sizeof(fmt_buf), "User%d", g_face_count + 1);
    FM225_EnrollFace(fmt_buf, dir);
}

static void Page_LightSensor(KeyEvent_t key, uint8_t *exit_flag)
{
    if (key == KEY_BACK)
    {
        *exit_flag = 1;
        return;
    }

    g_light_value = LightSensor_ReadAnalog();

    OLED_ShowString(0, 0, "Light Sensor", OLED_8X16);
    OLED_DrawLine(0, 16, 128, 16);

    snprintf(fmt_buf, sizeof(fmt_buf), "ADC: %d", g_light_value);
    OLED_ShowString(4, 20, fmt_buf, OLED_8X16);

    OLED_DrawRectangle(4, 40, 120, 10, OLED_UNFILLED);

    {
        uint16_t bar_w = (uint16_t)((uint32_t)g_light_value * 116 / 4095);
        if (bar_w > 116)
            bar_w = 116;
        if (bar_w > 0)
            OLED_DrawRectangle(6, 42, bar_w, 6, OLED_FILLED);
    }

    if (g_light_value < (uint16_t)g_light_threshold)
        OLED_ShowString(4, 54, "FillLight: ON", OLED_6X8);
    else
        OLED_ShowString(4, 54, "FillLight: OFF", OLED_6X8);
}

static void Page_LightThreshold(KeyEvent_t key, uint8_t *exit_flag)
{
    if (key == KEY_BACK)
    {
        *exit_flag = 1;
        return;
    }

    OLED_ShowString(0, 0, "Light Threshold", OLED_8X16);
    OLED_DrawLine(0, 16, 128, 16);

    snprintf(fmt_buf, sizeof(fmt_buf), "Threshold: %d", (int)g_light_threshold);
    OLED_ShowString(4, 24, fmt_buf, OLED_8X16);

    g_light_value = LightSensor_ReadAnalog();
    snprintf(fmt_buf, sizeof(fmt_buf), "Current: %d", g_light_value);
    OLED_ShowString(4, 44, fmt_buf, OLED_6X8);

    OLED_ShowString(4, 56, "UP/DN:Adj Step100", OLED_6X8);

    if (key == KEY_UP)
    {
        g_light_threshold += 100;
        if (g_light_threshold > 4095)
            g_light_threshold = 4095;
    }
    else if (key == KEY_DOWN)
    {
        g_light_threshold -= 100;
        if (g_light_threshold < 0)
            g_light_threshold = 0;
    }
}

static void Page_IRSensor(KeyEvent_t key, uint8_t *exit_flag)
{
    uint8_t ir_effective_mask;

    if (key == KEY_BACK)
    {
        *exit_flag = 1;
        return;
    }

    g_ir_status = SR505_ReadFiltered();

    OLED_ShowString(0, 0, "IR Sensor", OLED_8X16);
    OLED_DrawLine(0, 16, 128, 16);

    if (g_ir_status)
        OLED_ShowString(4, 22, "IR: DETECTED", OLED_8X16);
    else
        OLED_ShowString(4, 22, "IR: IDLE", OLED_8X16);

    ir_effective_mask = (g_ir_manual_mask || SR505_IsWarmupMasking()) ? 1U : 0U;
    if (ir_effective_mask)
        OLED_ShowString(4, 42, "Shield: ON", OLED_8X16);
    else
        OLED_ShowString(4, 42, "Shield: OFF", OLED_8X16);

    OLED_ShowString(4, 56, "BACK:Return", OLED_6X8);
}

/* -------- 2b. 红外屏蔽页面 -------- */
static void Page_IRShield(KeyEvent_t key, uint8_t *exit_flag)
{
    uint8_t ir_effective_mask;

    if (key == KEY_BACK)
    {
        *exit_flag = 1;
        return;
    }

    if (key == KEY_ENTER || key == KEY_UP || key == KEY_DOWN)
    {
        g_ir_manual_mask = !g_ir_manual_mask;
    }

    OLED_ShowString(0, 0, "IR Shield", OLED_8X16);
    OLED_DrawLine(0, 16, 128, 16);

    ir_effective_mask = (g_ir_manual_mask || SR505_IsWarmupMasking()) ? 1U : 0U;
    if (ir_effective_mask)
        OLED_ShowString(4, 24, "State: BLOCKED", OLED_8X16);
    else
        OLED_ShowString(4, 24, "State: ACTIVE", OLED_8X16);

    if (SR505_IsWarmupMasking())
        OLED_ShowString(4, 44, "Warmup blocking", OLED_6X8);
    else if (g_ir_manual_mask)
        OLED_ShowString(4, 44, "Manual blocking", OLED_6X8);
    else
        OLED_ShowString(4, 44, "Not blocked", OLED_6X8);

    OLED_ShowString(4, 52, "ENTER:Toggle", OLED_6X8);
    OLED_ShowString(4, 56, "BACK: Return", OLED_6X8);
}

static void Page_FaceVerify(KeyEvent_t key, uint8_t *exit_flag)
{
    if (key == KEY_BACK)
    {
        g_verify_state = VERIFY_IDLE;
        FillLight_Unlock();
        *exit_flag = 1;
        return;
    }

    OLED_ShowString(0, 0, "Face Verify", OLED_8X16);
    OLED_DrawLine(0, 16, 128, 16);

    switch (g_verify_state)
    {
    case VERIFY_IDLE:
        OLED_ShowString(4, 24, "Face the camera", OLED_8X16);
        OLED_ShowString(4, 44, "Press OK start", OLED_6X8);
        if (key == KEY_ENTER)
        {
            FillLight_LockIfNeeded();
            g_verify_state = VERIFY_WAITING;
            FM225_VerifyFace();
        }
        break;

    case VERIFY_WAITING:
        OLED_ShowString(8, 24, "Verifying...", OLED_8X16);
        OLED_ShowString(4, 44, "Face the camera", OLED_6X8);
        break;

    case VERIFY_SUCCESS:
        OLED_ShowString(8, 20, "Pass!", OLED_8X16);
        snprintf(fmt_buf, sizeof(fmt_buf), "ID:%d", g_verify_face_id);
        OLED_ShowString(8, 38, fmt_buf, OLED_8X16);
        OLED_ShowString(4, 56, "OK:Back", OLED_6X8);
        if (key == KEY_ENTER)
        {
            g_verify_state = VERIFY_IDLE;
            FillLight_Unlock();
            *exit_flag = 1;
        }
        break;

    case VERIFY_FAIL:
        OLED_ShowString(8, 24, "Denied!", OLED_8X16);
        OLED_ShowString(4, 44, "Not recognized", OLED_6X8);
        if (key == KEY_ENTER)
        {
            g_verify_state = VERIFY_IDLE;
            FillLight_Unlock();
            *exit_flag = 1;
        }
        break;

    case VERIFY_ERROR:
        OLED_ShowString(8, 24, "Error!", OLED_8X16);
        snprintf(fmt_buf, sizeof(fmt_buf), "Code: 0x%02X", g_enroll_err);
        OLED_ShowString(4, 44, fmt_buf, OLED_6X8);
        if (key == KEY_ENTER)
        {
            g_verify_state = VERIFY_IDLE;
            FillLight_Unlock();
            *exit_flag = 1;
        }
        break;
    }
}

static void Page_FaceEnroll(KeyEvent_t key, uint8_t *exit_flag)
{
    if (key == KEY_BACK)
    {
        g_enroll_state = ENROLL_IDLE;
        FillLight_Unlock();
        *exit_flag = 1;
        return;
    }

    OLED_ShowString(0, 0, "Face Enroll", OLED_8X16);
    OLED_DrawLine(0, 16, 128, 16);

    switch (g_enroll_state)
    {
    case ENROLL_IDLE:
        OLED_ShowString(4, 24, "Press OK to", OLED_8X16);
        OLED_ShowString(4, 42, "start enroll", OLED_8X16);
        if (key == KEY_ENTER)
        {
            FillLight_LockIfNeeded();
            g_enroll_state = ENROLL_MIDDLE;
            g_enroll_face_id = -1;
            g_enroll_err = 0;
            Enroll_StartNextStep();
        }
        break;

    case ENROLL_MIDDLE:
    case ENROLL_RIGHT:
    case ENROLL_LEFT:
    case ENROLL_DOWN:
    case ENROLL_UP:
    {
        uint8_t step = (uint8_t)(g_enroll_state - ENROLL_MIDDLE + 1);
        snprintf(fmt_buf, sizeof(fmt_buf), "Step %d/5", step);
        OLED_ShowString(4, 20, fmt_buf, OLED_8X16);

        OLED_ShowString(4, 40, (char *)Enroll_GetPrompt(g_enroll_state), OLED_8X16);

        OLED_ShowString(4, 56, "Recording...", OLED_6X8);
        break;
    }

    case ENROLL_DONE:
        OLED_ShowString(4, 20, "Enroll OK!", OLED_8X16);
        snprintf(fmt_buf, sizeof(fmt_buf), "FaceID: %d", g_enroll_face_id);
        OLED_ShowString(4, 40, fmt_buf, OLED_8X16);
        OLED_ShowString(4, 56, "OK:Confirm", OLED_6X8);
        if (key == KEY_ENTER)
        {
            g_enroll_state = ENROLL_IDLE;
            FillLight_Unlock();
            *exit_flag = 1;
        }
        break;

    case ENROLL_FAIL:
        OLED_ShowString(4, 20, "Enroll Fail!", OLED_8X16);
        snprintf(fmt_buf, sizeof(fmt_buf), "Err: 0x%02X", g_enroll_err);
        OLED_ShowString(4, 40, fmt_buf, OLED_8X16);
        OLED_ShowString(4, 56, "OK:Retry", OLED_6X8);
        if (key == KEY_ENTER)
        {
            g_enroll_state = ENROLL_IDLE;
        }
        break;
    }
}

static void Page_FaceDelete(KeyEvent_t key, uint8_t *exit_flag)
{
    static uint8_t del_confirm = 0;
    static uint8_t del_result = 0;

    if (key == KEY_BACK)
    {
        del_confirm = 0;
        del_result = 0;
        *exit_flag = 1;
        return;
    }

    OLED_ShowString(0, 0, "Delete Face", OLED_8X16);
    OLED_DrawLine(0, 16, 128, 16);

    if (del_result == 1)
    {
        OLED_ShowString(4, 24, "Deleted OK!", OLED_8X16);
        OLED_ShowString(4, 56, "OK:Continue", OLED_6X8);
        if (key == KEY_ENTER)
        {
            del_result = 0;
            FM225_GetFaceCount();
        }
        return;
    }

    if (del_confirm)
    {
        snprintf(fmt_buf, sizeof(fmt_buf), "Delete ID %d ?", g_del_face_id);
        OLED_ShowString(4, 24, fmt_buf, OLED_8X16);
        OLED_ShowString(4, 44, "OK:Yes  Back:No", OLED_6X8);

        if (key == KEY_ENTER)
        {
            FM225_DeleteFace(g_del_face_id);
            del_result = 1;
            del_confirm = 0;
        }
        else if (key == KEY_BACK)
        {
            del_confirm = 0;
        }
        return;
    }

    if (g_face_count == 0)
    {
        OLED_ShowString(4, 28, "No faces stored", OLED_8X16);
        return;
    }

    snprintf(fmt_buf, sizeof(fmt_buf), "Select ID: %d", g_del_face_id);
    OLED_ShowString(4, 24, fmt_buf, OLED_8X16);

    snprintf(fmt_buf, sizeof(fmt_buf), "Total: %d faces", g_face_count);
    OLED_ShowString(4, 44, fmt_buf, OLED_6X8);
    OLED_ShowString(4, 56, "UP/DN:Sel OK:Del", OLED_6X8);

    if (key == KEY_UP)
    {
        g_del_face_id++;
        if (g_del_face_id > 50)
            g_del_face_id = 1;
    }
    else if (key == KEY_DOWN)
    {
        g_del_face_id--;
        if (g_del_face_id < 1)
            g_del_face_id = 50;
    }
    else if (key == KEY_ENTER)
    {
        del_confirm = 1;
    }
}

static void Page_FaceDB(KeyEvent_t key, uint8_t *exit_flag)
{
    if (key == KEY_BACK)
    {
        *exit_flag = 1;
        return;
    }

    if (key == KEY_ENTER)
    {
        FM225_GetFaceCount();
    }

    OLED_ShowString(0, 0, "Face Database", OLED_8X16);
    OLED_DrawLine(0, 16, 128, 16);

    snprintf(fmt_buf, sizeof(fmt_buf), "Registered: %d", g_face_count);
    OLED_ShowString(4, 24, fmt_buf, OLED_8X16);

    if (g_face_count > 0)
    {
        snprintf(fmt_buf, sizeof(fmt_buf), "ID: 1 ~ %d", g_face_count);
        OLED_ShowString(4, 44, fmt_buf, OLED_8X16);
    }
    else
    {
        OLED_ShowString(4, 44, "Database empty", OLED_8X16);
    }

    OLED_ShowString(4, 56, "OK:Refresh", OLED_6X8);
}

static void Page_RFIDVerify(KeyEvent_t key, uint8_t *exit_flag)
{
    RC522_Status_t st;

    if (key == KEY_BACK)
    {
        g_rfid_state = 0;
        *exit_flag = 1;
        return;
    }

    OLED_ShowString(0, 0, "RFID Verify", OLED_8X16);
    OLED_DrawLine(0, 16, 128, 16);

    switch (g_rfid_state)
    {
    case 0:
        OLED_ShowString(4, 24, "Waiting card..", OLED_8X16);
        OLED_ShowString(4, 44, "Tap your card", OLED_6X8);

        st = RC522_DetectCard(&g_rfid_card);
        if (st == RC522_OK)
        {
            if (RC522_IsCardAuthorized(&g_rfid_card))
            {
                g_rfid_state = 2;
                App_AccessGranted(0, "RFID");
            }
            else
            {
                g_rfid_state = 3;
                App_AccessDenied();
            }
            RC522_HaltCard();
        }
        break;

    case 2:
        OLED_ShowString(8, 20, "Access OK!", OLED_8X16);
        snprintf(fmt_buf, sizeof(fmt_buf), "UID:%02X%02X%02X%02X",
                 g_rfid_card.uid[0], g_rfid_card.uid[1],
                 g_rfid_card.uid[2], g_rfid_card.uid[3]);
        OLED_ShowString(4, 40, fmt_buf, OLED_6X8);
        OLED_ShowString(4, 56, "OK:Back", OLED_6X8);
        if (key == KEY_ENTER)
        {
            g_rfid_state = 0;
            *exit_flag = 1;
        }
        break;

    case 3:
        OLED_ShowString(8, 20, "Denied!", OLED_8X16);
        snprintf(fmt_buf, sizeof(fmt_buf), "UID:%02X%02X%02X%02X",
                 g_rfid_card.uid[0], g_rfid_card.uid[1],
                 g_rfid_card.uid[2], g_rfid_card.uid[3]);
        OLED_ShowString(4, 40, fmt_buf, OLED_6X8);
        OLED_ShowString(4, 56, "OK:Back", OLED_6X8);
        if (key == KEY_ENTER)
        {
            g_rfid_state = 0;
            *exit_flag = 1;
        }
        break;
    }
}

static void Page_RFIDEnroll(KeyEvent_t key, uint8_t *exit_flag)
{
    RC522_Status_t st;

    if (key == KEY_BACK)
    {
        g_rfid_state = 0;
        *exit_flag = 1;
        return;
    }

    OLED_ShowString(0, 0, "RFID Enroll", OLED_8X16);
    OLED_DrawLine(0, 16, 128, 16);

    switch (g_rfid_state)
    {
    case 0:
        OLED_ShowString(4, 24, "Tap new card..", OLED_8X16);
        OLED_ShowString(4, 44, "to register", OLED_6X8);

        st = RC522_DetectCard(&g_rfid_card);
        if (st == RC522_OK)
        {
            st = RC522_AddCard(&g_rfid_card);
            if (st == RC522_OK)
            {
                g_rfid_state = 2;
                App_EnrollSuccessFeedback();
            }
            else if (st == RC522_CARD_ALREADY_EXIST)
                g_rfid_state = 4;
            else
                g_rfid_state = 5;
            RC522_HaltCard();
        }
        break;

    case 2:
        OLED_ShowString(8, 20, "Enrolled!", OLED_8X16);
        snprintf(fmt_buf, sizeof(fmt_buf), "UID:%02X%02X%02X%02X",
                 g_rfid_card.uid[0], g_rfid_card.uid[1],
                 g_rfid_card.uid[2], g_rfid_card.uid[3]);
        OLED_ShowString(4, 40, fmt_buf, OLED_6X8);
        snprintf(fmt_buf, sizeof(fmt_buf), "Total:%d cards", RC522_GetCardCount());
        OLED_ShowString(4, 50, fmt_buf, OLED_6X8);
        OLED_ShowString(4, 56, "OK:Continue", OLED_6X8);
        if (key == KEY_ENTER)
            g_rfid_state = 0;
        break;

    case 4:
        OLED_ShowString(4, 24, "Card exists!", OLED_8X16);
        OLED_ShowString(4, 56, "OK:Retry", OLED_6X8);
        if (key == KEY_ENTER)
            g_rfid_state = 0;
        break;

    case 5:
        OLED_ShowString(4, 24, "DB Full/Error!", OLED_8X16);
        OLED_ShowString(4, 56, "OK:Back", OLED_6X8);
        if (key == KEY_ENTER)
        {
            g_rfid_state = 0;
            *exit_flag = 1;
        }
        break;
    }
}

static void Page_RFIDDelete(KeyEvent_t key, uint8_t *exit_flag)
{
    static uint8_t del_confirm = 0;
    static uint8_t del_result = 0;

    if (key == KEY_BACK && !del_confirm)
    {
        del_result = 0;
        *exit_flag = 1;
        return;
    }

    OLED_ShowString(0, 0, "Delete Card", OLED_8X16);
    OLED_DrawLine(0, 16, 128, 16);

    uint8_t card_count = RC522_GetCardCount();

    if (del_result == 1)
    {
        OLED_ShowString(4, 24, "Deleted OK!", OLED_8X16);
        OLED_ShowString(4, 56, "OK:Continue", OLED_6X8);
        if (key == KEY_ENTER)
            del_result = 0;
        return;
    }

    if (del_confirm)
    {
        snprintf(fmt_buf, sizeof(fmt_buf), "Delete #%d ?", g_del_card_idx + 1);
        OLED_ShowString(4, 24, fmt_buf, OLED_8X16);
        OLED_ShowString(4, 44, "OK:Yes  Back:No", OLED_6X8);

        if (key == KEY_ENTER)
        {
            RC522_CardInfo_t card;
            if (RC522_GetCardByIndex(g_del_card_idx, &card) == RC522_OK)
            {
                RC522_RemoveCard(&card);
                del_result = 1;
            }
            del_confirm = 0;
        }
        else if (key == KEY_BACK)
        {
            del_confirm = 0;
        }
        return;
    }

    if (card_count == 0)
    {
        OLED_ShowString(4, 28, "No cards stored", OLED_8X16);
        return;
    }

    if (g_del_card_idx >= card_count)
        g_del_card_idx = 0;

    {
        RC522_CardInfo_t card;
        if (RC522_GetCardByIndex(g_del_card_idx, &card) == RC522_OK)
        {
            snprintf(fmt_buf, sizeof(fmt_buf), "#%d UID:%02X%02X%02X%02X",
                     g_del_card_idx + 1,
                     card.uid[0], card.uid[1], card.uid[2], card.uid[3]);
            OLED_ShowString(4, 22, fmt_buf, OLED_6X8);
        }
    }

    snprintf(fmt_buf, sizeof(fmt_buf), "Total: %d cards", card_count);
    OLED_ShowString(4, 36, fmt_buf, OLED_6X8);
    OLED_ShowString(4, 56, "UP/DN:Sel OK:Del", OLED_6X8);

    if (key == KEY_UP)
    {
        if (g_del_card_idx < card_count - 1)
            g_del_card_idx++;
        else
            g_del_card_idx = 0;
    }
    else if (key == KEY_DOWN)
    {
        if (g_del_card_idx > 0)
            g_del_card_idx--;
        else
            g_del_card_idx = card_count - 1;
    }
    else if (key == KEY_ENTER)
    {
        del_confirm = 1;
    }
}

static void Page_ESPHistory(KeyEvent_t key, uint8_t *exit_flag)
{
    uint8_t i;

    if (key == KEY_BACK)
    {
        *exit_flag = 1;
        return;
    }

    OLED_ShowString(0, 0, "ESP History", OLED_8X16);
    OLED_DrawLine(0, 16, 128, 16);

    if (g_esp_history_count == 0)
    {
        OLED_ShowString(4, 28, "No commands yet", OLED_8X16);
        return;
    }

    for (i = 0; i < g_esp_history_count && i < APP_ESP_HISTORY_MAX; i++)
    {
        snprintf(fmt_buf, sizeof(fmt_buf), "%d:%s", i + 1, g_esp_history[i]);
        OLED_ShowString(4, 18 + i * 14, fmt_buf, OLED_6X8);
    }
}

static void Action_DeleteAllFaces(void)
{
    FM225_DeleteAllFaces();
    g_face_count = 0;

    OLED_Clear();
    OLED_ShowString(4, 20, "All faces", OLED_8X16);
    OLED_ShowString(4, 40, "deleted!", OLED_8X16);
    OLED_Update();
    HAL_Delay(1500);
}

static void Action_DeleteAllCards(void)
{
    RC522_ClearAllCards();

    OLED_Clear();
    OLED_ShowString(4, 20, "All cards", OLED_8X16);
    OLED_ShowString(4, 40, "deleted!", OLED_8X16);
    OLED_Update();
    HAL_Delay(1500);
}

static void Action_ToggleCursor(void)
{
    MF_CursorStyle_t style = MF_GetCursorStyle();
    if (style == MF_CURSOR_REVERSE)
        MF_SetCursorStyle(MF_CURSOR_BOX);
    else
        MF_SetCursorStyle(MF_CURSOR_REVERSE);

    style = MF_GetCursorStyle();
    OLED_Clear();
    OLED_ShowString(4, 20, "Cursor Style:", OLED_8X16);
    OLED_ShowString(4, 40, (char *)cursor_names[style], OLED_8X16);
    OLED_Update();
    HAL_Delay(800);
}

static void App_IWDG_Init(void)
{
#if APP_USE_IWDG
    hiwdg_app.Instance = IWDG;
    hiwdg_app.Init.Prescaler = IWDG_PRESCALER_256;
    hiwdg_app.Init.Reload = 1250U;
    if (HAL_IWDG_Init(&hiwdg_app) != HAL_OK)
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
    (void)HAL_IWDG_Refresh(&hiwdg_app);
#endif
}

static void Menu_BuildTree(void)
{

    MF_Menu_t *menuMain = MF_CreateMenu("Main Menu");

    MF_Menu_t *menuFace = MF_CreateMenu("Face Manage");

    MF_Menu_t *menuFaceDel = MF_CreateMenu("Delete Face");

    MF_Menu_t *menuRFID = MF_CreateMenu("RFID Manage");

    MF_Menu_t *menuRFIDDel = MF_CreateMenu("Delete Card");

    MF_Menu_t *menuLight = MF_CreateMenu("Light Option");
    MF_Menu_t *menuIR = MF_CreateMenu("IR Sensor");

    MF_AddSubmenu(menuMain, "Light Option", menuLight);
    MF_AddSubmenu(menuMain, "IR Sensor", menuIR);
    MF_AddSubmenu(menuMain, "Face Manage", menuFace);
    MF_AddSubmenu(menuMain, "RFID Manage", menuRFID);
    MF_AddCustomPage(menuMain, "ESP History", Page_ESPHistory);
    MF_AddAction(menuMain, "Cursor Style", Action_ToggleCursor);

    /* --- IR Sensor submenu --- */
    MF_AddCustomPage(menuIR, "Sensor Status", Page_IRSensor);
    MF_AddCustomPage(menuIR, "Manual Shield", Page_IRShield);

    MF_AddCustomPage(menuFace, "Verify Face", Page_FaceVerify);
    MF_AddCustomPage(menuFace, "Enroll Face", Page_FaceEnroll);
    MF_AddSubmenu(menuFace, "Delete Face", menuFaceDel);
    MF_AddCustomPage(menuFace, "Face Library", Page_FaceDB);

    MF_AddCustomPage(menuFaceDel, "Select Delete", Page_FaceDelete);
    MF_AddAction(menuFaceDel, "Delete All", Action_DeleteAllFaces);

    MF_AddCustomPage(menuRFID, "Verify Card", Page_RFIDVerify);
    MF_AddCustomPage(menuRFID, "Enroll Card", Page_RFIDEnroll);
    MF_AddSubmenu(menuRFID, "Delete Card", menuRFIDDel);

    MF_AddCustomPage(menuRFIDDel, "Select Delete", Page_RFIDDelete);
    MF_AddAction(menuRFIDDel, "Delete All", Action_DeleteAllCards);

    MF_AddCustomPage(menuLight, "Light Data", Page_LightSensor);
    MF_AddCustomPage(menuLight, "Threshold", Page_LightThreshold);

    MF_Start(menuMain);
}

void App_Init(void)
{
    HAL_Init();

    sys_stm32_clock_init(RCC_PLL_MUL9);
    delay_init(72);

    OLED_Init();
    Key_Init();
    SR505_Init();
    LightSensor_Init();
    Buzzer_Init();
    LED_Init();
    Servo_Init();
    RC522_Init();
    FM225_Init();
    ESP_Init();

    Servo_SetAngle(APP_DOOR_CLOSE_ANGLE);

    LED_Off(RED_LED_GPIO_PORT, RED_LED_PIN);
    LED_Off(GREEN_LED_GPIO_PORT, GREEN_LED_PIN);
    LED_Off(FILLLIGHT_LED_GPIO_PORT, FILLLIGHT_LED_PIN);
    Buzzer_Off();

    FM225_SetMatchedCallback(OnFaceMatched);
    FM225_SetUnmatchedCallback(OnFaceUnmatched);
    FM225_SetInvalidCallback(OnFaceInvalid);
    FM225_SetEnrollDoneCallback(OnEnrollDone);
    FM225_SetEnrollFailCallback(OnEnrollFail);
    FM225_SetFaceCountCallback(OnFaceCount);

    ESP_RegisterCallback(OnESPData);

    Menu_BuildTree();

    FM225_GetFaceCount();

    OLED_Clear();
    OLED_ShowString(16, 8, "Smart Access", OLED_8X16);
    OLED_ShowString(12, 28, "Control System", OLED_8X16);
    OLED_ShowString(24, 52, "Initializing..", OLED_6X8);
    OLED_Update();
    HAL_Delay(1500);

    App_IWDG_Init();
    App_IWDG_Feed();
}

void App_Loop(void)
{
    g_ir_prev = g_ir_status;
    g_ir_status = SR505_ReadFiltered();
    g_light_value = LightSensor_ReadAnalog();

    FM225_Process();
    ESP_Process();

    App_AutoLightControl();
    App_AutoIRTrigger();

    App_AutoStatusUpload();

    App_FeedbackUpdate();

    MF_CustomPageCb cur_page = MF_GetCustomPageCb();
    if (g_verify_state != VERIFY_WAITING && g_enroll_state == ENROLL_IDLE && g_rfid_state == 0 && !g_rfid_auto_detected && cur_page != Page_RFIDEnroll && cur_page != Page_RFIDDelete && cur_page != Page_RFIDVerify && cur_page != Page_FaceVerify && cur_page != Page_FaceEnroll)
    {
        RC522_CardInfo_t card;
        RC522_Status_t st = RC522_DetectCard(&card);
        if (st == RC522_OK)
        {
            memcpy(&g_rfid_card, &card, sizeof(RC522_CardInfo_t));
            g_rfid_auto_detected = 1;
            g_rfid_state = 0;

            if (RC522_IsCardAuthorized(&g_rfid_card))
            {
                g_rfid_state = 2;
                App_AccessGranted(0, "RFID");
            }
            else
            {
                g_rfid_state = 3;
                App_AccessDenied();
            }
            RC522_HaltCard();
            MF_EnterCustomPage(Page_RFIDVerify);
        }
        else
        {
            g_rfid_auto_detected = 0;
        }
    }
    else if (g_rfid_auto_detected && g_rfid_state == 0)
    {
        g_rfid_auto_detected = 0;
    }

    MF_Loop();

    App_IWDG_Feed();
}
