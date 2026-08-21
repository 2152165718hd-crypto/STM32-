#include ".\Application\Menu\Menu.h"
#include <stdio.h>

/* ====================================================================
 * STM32 人脸/指纹门禁系统 - 菜单功能模块
 * 基于 MenuFramework 的页面与业务逻辑驱动
 * ==================================================================== */

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

static uint8_t g_door_open = 0;
static uint32_t g_door_open_tick = 0;
static uint8_t g_buzzer_err = 0;
static uint32_t g_buzzer_err_tick = 0;
static uint8_t g_feedback_active = 0;
static uint32_t g_feedback_tick = 0;

static int16_t g_del_face_id = 1;

static uint8_t g_del_card_idx = 0;

/* ---- AS608 指纹相关状态 ---- */
static uint8_t g_fp_state = 0;         /* 指纹页面状态 */
static uint16_t g_fp_match_id = 0;     /* 匹配成功的 PageID */
static uint16_t g_fp_match_score = 0;  /* 匹配分数 */
static uint16_t g_fp_enroll_id = 0;    /* 当前录入的 PageID */
static uint8_t g_fp_auto_detected = 0; /* WAK 自动触发标志 */
static uint16_t g_fp_del_id = 0;       /* 待删除的指纹 ID */

static const char *cursor_names[] = {"Reverse", "Box"};

static char fmt_buf[40];

/* ---- 管理者模式状态 ---- */
static uint8_t g_admin_mode = 0;                                   /* 0=普通, 1=管理者 */
static char g_admin_password[APP_PWD_MAX_LEN + 1] = APP_PWD_DEFAULT; /* 当前密码 */

/* ---- 密码输入页面状态 ---- */
static char g_pwd_buf[APP_PWD_MAX_LEN + 1];  /* 输入缓冲 */
static uint8_t g_pwd_len = 0;                 /* 已输入位数 */
static uint8_t g_pwd_digit = 0;               /* 当前滚动数字 0~9 */
static uint32_t g_pwd_last_enter_tick = 0;     /* 上次 ENTER 按下时间 */
static uint32_t g_pwd_last_back_tick = 0;      /* 上次 BACK 按下时间 */
static uint8_t g_pwd_change_mode = 0;          /* 0=验证密码, 1=修改密码 */
static uint8_t g_pwd_result = 0;               /* 密码验证结果提示: 0=无, 1=成功, 2=失败 */
static uint32_t g_pwd_result_tick = 0;          /* 结果提示开始时间 */

#if APP_USE_IWDG
static IWDG_HandleTypeDef hiwdg_app;
#endif

/* ---- OneNET 云平台相关状态 ---- */
static uint32_t g_onenet_last_report_tick = 0;
static uint32_t g_onenet_pending_retry_tick = 0;
static uint8_t g_onenet_immediate_report_pending = 0U;

/* ---- ESP 上传日志（最近5条） ---- */
#define ESP_LOG_MAX 5
typedef struct
{
    uint32_t tick;       /* 上传成功时的 HAL_GetTick */
    uint8_t  valid;      /* 是否有效 */
} ESPUploadLog_t;

static ESPUploadLog_t g_upload_log[ESP_LOG_MAX];
static uint8_t g_upload_log_idx = 0;          /* 下一个写入位置（环形） */
static uint32_t g_upload_total_count = 0;      /* 累计成功次数 */

/* 页面回调函数声明 */
static void Page_FaceVerify(KeyEvent_t key, uint8_t *exit_flag);
static void Page_FaceEnroll(KeyEvent_t key, uint8_t *exit_flag);
static void Page_FaceDelete(KeyEvent_t key, uint8_t *exit_flag);
static void Page_FaceDB(KeyEvent_t key, uint8_t *exit_flag);
static void Page_RFIDVerify(KeyEvent_t key, uint8_t *exit_flag);
static void Page_RFIDEnroll(KeyEvent_t key, uint8_t *exit_flag);
static void Page_RFIDDelete(KeyEvent_t key, uint8_t *exit_flag);
static void Page_RFIDDB(KeyEvent_t key, uint8_t *exit_flag);
static void Page_FPVerify(KeyEvent_t key, uint8_t *exit_flag);
static void Page_FPEnroll(KeyEvent_t key, uint8_t *exit_flag);
static void Page_FPDelete(KeyEvent_t key, uint8_t *exit_flag);
static void Page_FPInfo(KeyEvent_t key, uint8_t *exit_flag);
static void Page_PasswordInput(KeyEvent_t key, uint8_t *exit_flag);
static void Page_ChangePassword(KeyEvent_t key, uint8_t *exit_flag);
static void Page_ESPUploadLog(KeyEvent_t key, uint8_t *exit_flag);
static void Page_ESPDebugInfo(KeyEvent_t key, uint8_t *exit_flag);

/* 动作函数声明 */
static void Action_DeleteAllFaces(void);
static void Action_DeleteAllCards(void);
static void Action_DeleteAllFP(void);
static void Action_ToggleCursor(void);
static void Action_ExitAdmin(void);

/* 应用功能函数声明 */
static void App_DoorOpen(void);
static void App_AccessGranted(int16_t id, const char *name);
static void App_AccessDenied(void);
static void App_FailFeedback(void);
static void App_FeedbackUpdate(void);
static void Menu_Rebuild(void);
static void App_EnrollSuccessFeedback(void);
static void App_IWDG_Init(void);
static void App_IWDG_Feed(void);
static void App_OnServoControl(uint8_t servo_on);
static void App_OnPasswordSet(const char *password);
static uint8_t App_ReportTelemetry(void);
static void App_RequestImmediateTelemetry(void);

static FM225_FaceDirection_t Enroll_GetDirection(EnrollState_t state);
static const char *Enroll_GetPrompt(EnrollState_t state);
static void Enroll_StartNextStep(void);

/* FM225 回调函数 */
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
    App_FailFeedback();
}

static void OnFaceCount(uint8_t count)
{
    g_face_count = count;
}

/* 门禁反馈控制 */

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
}

static void App_FailFeedback(void)
{
    LED_On(RED_LED_GPIO_PORT, RED_LED_PIN);
    LED_Off(GREEN_LED_GPIO_PORT, GREEN_LED_PIN);

    g_buzzer_err = 1;
    g_buzzer_err_tick = HAL_GetTick();
    Buzzer_On();

    g_feedback_active = 1;
    g_feedback_tick = HAL_GetTick();
}

static void App_AccessDenied(void)
{
    App_FailFeedback();
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
        if (now - g_feedback_tick >= 3000)
        {
            LED_Off(RED_LED_GPIO_PORT, RED_LED_PIN);
            LED_Off(GREEN_LED_GPIO_PORT, GREEN_LED_PIN);
            g_feedback_active = 0;
        }
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

static void Page_FaceVerify(KeyEvent_t key, uint8_t *exit_flag)
{
    if (key == KEY_BACK)
    {
        g_verify_state = VERIFY_IDLE;
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
            *exit_flag = 1;
        }
        break;

    case VERIFY_FAIL:
        OLED_ShowString(8, 24, "Denied!", OLED_8X16);
        OLED_ShowString(4, 44, "Not recognized", OLED_6X8);
        if (key == KEY_ENTER)
        {
            g_verify_state = VERIFY_IDLE;
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
            {
                g_rfid_state = 4;
                App_FailFeedback();
            }
            else
            {
                g_rfid_state = 5;
                App_FailFeedback();
            }
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

static void Page_RFIDDB(KeyEvent_t key, uint8_t *exit_flag)
{
    static uint8_t view_card_idx = 0;

    if (key == KEY_BACK)
    {
        view_card_idx = 0;
        *exit_flag = 1;
        return;
    }

    OLED_ShowString(0, 0, "Card Overview", OLED_8X16);
    OLED_DrawLine(0, 16, 128, 16);

    uint8_t card_count = RC522_GetCardCount();

    if (card_count == 0)
    {
        OLED_ShowString(4, 28, "No cards stored", OLED_8X16);
        return;
    }

    if (view_card_idx >= card_count)
        view_card_idx = 0;

    RC522_CardInfo_t card;
    if (RC522_GetCardByIndex(view_card_idx, &card) == RC522_OK)
    {
        snprintf(fmt_buf, sizeof(fmt_buf), "Card %d/%d", view_card_idx + 1, card_count);
        OLED_ShowString(4, 22, fmt_buf, OLED_8X16);

        snprintf(fmt_buf, sizeof(fmt_buf), "UID:%02X%02X%02X%02X",
                 card.uid[0], card.uid[1], card.uid[2], card.uid[3]);
        OLED_ShowString(4, 40, fmt_buf, OLED_8X16);
    }

    OLED_ShowString(4, 56, "UP/DN:View", OLED_6X8);

    if (key == KEY_UP)
    {
        if (view_card_idx < card_count - 1)
            view_card_idx++;
        else
            view_card_idx = 0;
    }
    else if (key == KEY_DOWN)
    {
        if (view_card_idx > 0)
            view_card_idx--;
        else
            view_card_idx = card_count - 1;
    }
}

/* ==================== AS608 指纹页面 ==================== */

static void Page_FPVerify(KeyEvent_t key, uint8_t *exit_flag)
{
    uint8_t ensure;
    SearchResult result;

    if (key == KEY_BACK)
    {
        g_fp_state = 0;
        g_fp_auto_detected = 0;
        *exit_flag = 1;
        return;
    }

    OLED_ShowString(0, 0, "FP Verify", OLED_8X16);
    OLED_DrawLine(0, 16, 128, 16);

    switch (g_fp_state)
    {
    case 0: /* 等待按指纹 */
        OLED_ShowString(4, 24, "Place finger..", OLED_8X16);
        OLED_ShowString(4, 44, "on the sensor", OLED_6X8);

        ensure = GZ_GetImage();
        if (ensure == 0x00)
        {
            ensure = GZ_GenChar(CharBuffer1);
            if (ensure == 0x00)
            {
                ensure = GZ_HighSpeedSearch(CharBuffer1, 0, 300, &result);
                if (ensure == 0x00)
                {
                    g_fp_match_id = result.pageID;
                    g_fp_match_score = result.mathscore;
                    g_fp_state = 2; /* 识别成功 */
                    App_AccessGranted((int16_t)result.pageID, "FP");
                }
                else
                {
                    g_fp_state = 3; /* 识别失败 */
                    App_AccessDenied();
                }
            }
            else
            {
                g_fp_state = 3;
                App_FailFeedback();
            }
        }
        break;

    case 2: /* 通过 */
        OLED_ShowString(8, 20, "Pass!", OLED_8X16);
        snprintf(fmt_buf, sizeof(fmt_buf), "ID:%d  Score:%d", g_fp_match_id, g_fp_match_score);
        OLED_ShowString(4, 40, fmt_buf, OLED_6X8);
        OLED_ShowString(4, 56, "OK:Back", OLED_6X8);
        if (key == KEY_ENTER)
        {
            g_fp_state = 0;
            g_fp_auto_detected = 0;
            *exit_flag = 1;
        }
        break;

    case 3: /* 未通过 */
        OLED_ShowString(8, 24, "Denied!", OLED_8X16);
        OLED_ShowString(4, 44, "FP not matched", OLED_6X8);
        OLED_ShowString(4, 56, "OK:Back", OLED_6X8);
        if (key == KEY_ENTER)
        {
            g_fp_state = 0;
            g_fp_auto_detected = 0;
            *exit_flag = 1;
        }
        break;
    }
}

static void Page_FPEnroll(KeyEvent_t key, uint8_t *exit_flag)
{
    uint8_t ensure;

    if (key == KEY_BACK)
    {
        g_fp_state = 0;
        *exit_flag = 1;
        return;
    }

    OLED_ShowString(0, 0, "FP Enroll", OLED_8X16);
    OLED_DrawLine(0, 16, 128, 16);

    switch (g_fp_state)
    {
    case 0: /* 读取当前可用录入 ID */
    {
        uint16_t validN = 0;
        GZ_ValidTempleteNum(&validN);
        g_fp_enroll_id = validN; /* 默认按当前模板数量分配 ID */

        snprintf(fmt_buf, sizeof(fmt_buf), "New ID: %d", g_fp_enroll_id);
        OLED_ShowString(4, 20, fmt_buf, OLED_8X16);
        OLED_ShowString(4, 40, "Press OK start", OLED_8X16);
        if (key == KEY_ENTER)
            g_fp_state = 1;
        break;
    }

    case 1: /* 第一次采集 */
        OLED_ShowString(4, 20, "Step 1/2", OLED_8X16);
        OLED_ShowString(4, 40, "Place finger..", OLED_8X16);

        ensure = GZ_GetImage();
        if (ensure == 0x00)
        {
            ensure = GZ_GenChar(CharBuffer1);
            if (ensure == 0x00)
                g_fp_state = 2;
            else
            {
                g_fp_state = 6;
                App_FailFeedback();
            }
        }
        break;

    case 2: /* 提示抬手 */
        OLED_ShowString(4, 24, "Remove finger", OLED_8X16);
        OLED_ShowString(4, 44, "then press OK", OLED_6X8);
        if (key == KEY_ENTER)
            g_fp_state = 3;
        break;

    case 3: /* 第二次采集 */
        OLED_ShowString(4, 20, "Step 2/2", OLED_8X16);
        OLED_ShowString(4, 40, "Place again..", OLED_8X16);

        ensure = GZ_GetImage();
        if (ensure == 0x00)
        {
            ensure = GZ_GenChar(CharBuffer2);
            if (ensure == 0x00)
                g_fp_state = 4;
            else
            {
                g_fp_state = 6;
                App_FailFeedback();
            }
        }
        break;

    case 4: /* 合并特征并存储 */
        OLED_ShowString(4, 28, "Processing...", OLED_8X16);

        ensure = GZ_RegModel();
        if (ensure == 0x00)
        {
            ensure = GZ_StoreChar(CharBuffer1, g_fp_enroll_id);
            if (ensure == 0x00)
            {
                g_fp_state = 5;
                App_EnrollSuccessFeedback();
            }
            else
            {
                g_fp_state = 6;
                App_FailFeedback();
            }
        }
        else
        {
            g_fp_state = 6;
            App_FailFeedback();
        }
        break;

    case 5: /* 录入成功 */
        OLED_ShowString(4, 20, "Enroll OK!", OLED_8X16);
        snprintf(fmt_buf, sizeof(fmt_buf), "Saved ID: %d", g_fp_enroll_id);
        OLED_ShowString(4, 40, fmt_buf, OLED_8X16);
        OLED_ShowString(4, 56, "OK:Continue", OLED_6X8);
        if (key == KEY_ENTER)
            g_fp_state = 0;
        break;

    case 6: /* 录入失败 */
        OLED_ShowString(4, 20, "Enroll Fail!", OLED_8X16);
        OLED_ShowString(4, 40, "Please retry", OLED_8X16);
        OLED_ShowString(4, 56, "OK:Retry", OLED_6X8);
        if (key == KEY_ENTER)
            g_fp_state = 0;
        break;
    }
}

static void Page_FPDelete(KeyEvent_t key, uint8_t *exit_flag)
{
    static uint8_t del_confirm = 0;
    static uint8_t del_result = 0;

    if (key == KEY_BACK && !del_confirm)
    {
        del_result = 0;
        *exit_flag = 1;
        return;
    }

    OLED_ShowString(0, 0, "Delete FP", OLED_8X16);
    OLED_DrawLine(0, 16, 128, 16);

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
        snprintf(fmt_buf, sizeof(fmt_buf), "Delete ID %d ?", g_fp_del_id);
        OLED_ShowString(4, 24, fmt_buf, OLED_8X16);
        OLED_ShowString(4, 44, "OK:Yes  Back:No", OLED_6X8);

        if (key == KEY_ENTER)
        {
            uint8_t ensure = GZ_DeletChar(g_fp_del_id, 1);
            if (ensure == 0x00)
                del_result = 1;
            del_confirm = 0;
        }
        else if (key == KEY_BACK)
        {
            del_confirm = 0;
        }
        return;
    }

    /* 读取当前已录入模板数量 */
    uint16_t validN = 0;
    GZ_ValidTempleteNum(&validN);

    if (validN == 0)
    {
        OLED_ShowString(4, 28, "No FP stored", OLED_8X16);
        return;
    }

    snprintf(fmt_buf, sizeof(fmt_buf), "Select ID: %d", g_fp_del_id);
    OLED_ShowString(4, 24, fmt_buf, OLED_8X16);

    snprintf(fmt_buf, sizeof(fmt_buf), "Total: %d FPs", validN);
    OLED_ShowString(4, 44, fmt_buf, OLED_6X8);
    OLED_ShowString(4, 56, "UP/DN:Sel OK:Del", OLED_6X8);

    if (key == KEY_UP)
    {
        g_fp_del_id++;
        if (g_fp_del_id > 299)
            g_fp_del_id = 0;
    }
    else if (key == KEY_DOWN)
    {
        if (g_fp_del_id > 0)
            g_fp_del_id--;
        else
            g_fp_del_id = 299;
    }
    else if (key == KEY_ENTER)
    {
        del_confirm = 1;
    }
}

static void Page_FPInfo(KeyEvent_t key, uint8_t *exit_flag)
{
    static uint8_t loaded = 0;
    static uint16_t fp_valid = 0;
    static SysPara fp_sys;

    if (key == KEY_BACK)
    {
        loaded = 0;
        *exit_flag = 1;
        return;
    }

    OLED_ShowString(0, 0, "FP Info", OLED_8X16);
    OLED_DrawLine(0, 16, 128, 16);

    if (!loaded || key == KEY_ENTER)
    {
        GZ_ValidTempleteNum(&fp_valid);
        GZ_ReadSysPara(&fp_sys);
        loaded = 1;
    }

    snprintf(fmt_buf, sizeof(fmt_buf), "Stored: %d", fp_valid);
    OLED_ShowString(4, 20, fmt_buf, OLED_8X16);

    snprintf(fmt_buf, sizeof(fmt_buf), "Capacity: %d", fp_sys.GZ_max);
    OLED_ShowString(4, 38, fmt_buf, OLED_8X16);

    OLED_ShowString(4, 56, "OK:Refresh", OLED_6X8);
}

/* ==================== 密码输入页面 ==================== */

/**
 * @brief  密码输入页面初始化（进入页面时调用）
 */
static void PasswordPage_Init(uint8_t change_mode)
{
    memset(g_pwd_buf, 0, sizeof(g_pwd_buf));
    g_pwd_len = 0;
    g_pwd_digit = 0;
    g_pwd_last_enter_tick = 0;
    g_pwd_last_back_tick = 0;
    g_pwd_change_mode = change_mode;
    g_pwd_result = 0;
    g_pwd_result_tick = 0;
}

/**
 * @brief  绘制密码页面的数字滚轮 UI
 */
static void PasswordPage_DrawWheel(void)
{
    /* 上方小字体: 前一个数字 */
    uint8_t prev_digit = (g_pwd_digit == 0) ? 9 : g_pwd_digit - 1;
    uint8_t next_digit = (g_pwd_digit == 9) ? 0 : g_pwd_digit + 1;

    /* 居中显示在屏幕中间偏左的位置 */
    /* Row 0: y=0, 6x8 小字体 - 上方数字 */
    snprintf(fmt_buf, sizeof(fmt_buf), "%d", prev_digit);
    OLED_ShowString(60, 0, fmt_buf, OLED_6X8);

    /* Row 1: y=10, 8x16 大字体 - 当前选中数字（确认行）*/
    snprintf(fmt_buf, sizeof(fmt_buf), "%d", g_pwd_digit);
    OLED_ShowString(56, 10, fmt_buf, OLED_8X16);

    /* 在大字体数字两侧画选中指示 */
    OLED_ShowString(44, 10, "[", OLED_8X16);
    OLED_ShowString(66, 10, "]", OLED_8X16);

    /* Row 2: y=28, 6x8 小字体 - 下方数字 */
    snprintf(fmt_buf, sizeof(fmt_buf), "%d", next_digit);
    OLED_ShowString(60, 28, fmt_buf, OLED_6X8);

    /* 分隔线 */
    OLED_DrawLine(0, 37, 128, 37);

    /* Row 3: y=40, 密码输入框 - 显示已输入的密码 */
    OLED_ShowString(4, 40, "PWD:", OLED_8X16);
    if (g_pwd_len > 0)
    {
        /* 显示已输入的密码字符 */
        char disp[APP_PWD_MAX_LEN + 1];
        memset(disp, 0, sizeof(disp));
        memcpy(disp, g_pwd_buf, g_pwd_len);
        OLED_ShowString(36, 40, disp, OLED_8X16);
    }
    /* 显示光标下划线 */
    if (g_pwd_len < APP_PWD_MAX_LEN)
    {
        OLED_ShowString(36 + g_pwd_len * 8, 40, "_", OLED_8X16);
    }

    /* Row 4: y=56, 提示文字 */
    OLED_ShowString(0, 56, "OK:In 2OK:Go 2B:X", OLED_6X8);
}

static void Page_PasswordInput(KeyEvent_t key, uint8_t *exit_flag)
{
    static uint8_t pwd_inited = 0;
    uint32_t now = HAL_GetTick();

    /* 首次进入时初始化 */
    if (!pwd_inited)
    {
        PasswordPage_Init(0);
        pwd_inited = 1;
    }
    if (g_pwd_result != 0)
    {
        OLED_ShowString(0, 0, "Password", OLED_8X16);
        OLED_DrawLine(0, 16, 128, 16);

        if (g_pwd_result == 1)
        {
            OLED_ShowString(16, 24, "Access OK!", OLED_8X16);
            OLED_ShowString(8, 44, "Entering admin..", OLED_6X8);
        }
        else
        {
            OLED_ShowString(8, 24, "Wrong Password!", OLED_8X16);
            OLED_ShowString(16, 44, "Try again..", OLED_6X8);
        }

        if (now - g_pwd_result_tick >= 1500)
        {
            if (g_pwd_result == 1)
            {
                /* 验证成功 → 进入管理者模式 */
                g_admin_mode = 1;
                g_pwd_result = 0;
                pwd_inited = 0;
                *exit_flag = 1;
                Menu_Rebuild();
                return;
            }
            else
            {
                /* 验证失败 → 清空重试 */
                memset(g_pwd_buf, 0, sizeof(g_pwd_buf));
                g_pwd_len = 0;
                g_pwd_digit = 0;
                g_pwd_result = 0;
            }
        }
        return;
    }

    /* 双击 BACK 检测: 退出密码页面 */
    if (key == KEY_BACK)
    {
        if (now - g_pwd_last_back_tick <= APP_DOUBLE_CLICK_MS)
        {
            /* 双击 BACK → 退出 */
            g_pwd_last_back_tick = 0;
            pwd_inited = 0;
            *exit_flag = 1;
            return;
        }
        else
        {
            g_pwd_last_back_tick = now;
            /* 单击 BACK → 删除最后一位 */
            if (g_pwd_len > 0)
            {
                g_pwd_len--;
                g_pwd_buf[g_pwd_len] = '\0';
            }
        }
    }

    /* 双击 ENTER 检测: 提交密码 */
    if (key == KEY_ENTER)
    {
        if (now - g_pwd_last_enter_tick <= APP_DOUBLE_CLICK_MS)
        {
            /* 双击 ENTER → 验证密码 */
            g_pwd_last_enter_tick = 0;
            if (strcmp(g_pwd_buf, g_admin_password) == 0)
            {
                g_pwd_result = 1; /* 成功 */
                Buzzer_On();
                HAL_Delay(30);
                Buzzer_Off();
            }
            else
            {
                g_pwd_result = 2; /* 失败 */
                App_FailFeedback();
            }
            g_pwd_result_tick = now;
        }
        else
        {
            g_pwd_last_enter_tick = now;
            /* 单击 ENTER → 输入当前数字 */
            if (g_pwd_len < APP_PWD_MAX_LEN)
            {
                g_pwd_buf[g_pwd_len] = '0' + g_pwd_digit;
                g_pwd_len++;
                g_pwd_buf[g_pwd_len] = '\0';
            }
        }
    }

    /* UP/DOWN: 数字滚动 */
    if (key == KEY_UP)
    {
        g_pwd_digit = (g_pwd_digit == 9) ? 0 : g_pwd_digit + 1;
    }
    else if (key == KEY_DOWN)
    {
        g_pwd_digit = (g_pwd_digit == 0) ? 9 : g_pwd_digit - 1;
    }

    /* 绘制 UI */
    PasswordPage_DrawWheel();
}

/* ==================== 修改密码页面 ==================== */

static void Page_ChangePassword(KeyEvent_t key, uint8_t *exit_flag)
{
    static uint8_t cpwd_inited = 0;
    uint32_t now = HAL_GetTick();

    /* 首次进入时初始化 */
    if (!cpwd_inited)
    {
        PasswordPage_Init(1);
        cpwd_inited = 1;
    }

    /* 结果提示 */
    if (g_pwd_result != 0)
    {
        OLED_ShowString(0, 0, "Change PWD", OLED_8X16);
        OLED_DrawLine(0, 16, 128, 16);

        if (g_pwd_result == 1)
        {
            OLED_ShowString(8, 24, "Password Changed", OLED_8X16);
            OLED_ShowString(24, 44, "Success!", OLED_6X8);
        }

        if (now - g_pwd_result_tick >= 1500)
        {
            g_pwd_result = 0;
            cpwd_inited = 0;
            *exit_flag = 1;
        }
        return;
    }

    /* 双击 BACK 检测: 退出 */
    if (key == KEY_BACK)
    {
        if (now - g_pwd_last_back_tick <= APP_DOUBLE_CLICK_MS)
        {
            g_pwd_last_back_tick = 0;
            cpwd_inited = 0;
            *exit_flag = 1;
            return;
        }
        else
        {
            g_pwd_last_back_tick = now;
            if (g_pwd_len > 0)
            {
                g_pwd_len--;
                g_pwd_buf[g_pwd_len] = '\0';
            }
        }
    }

    /* 双击 ENTER: 确认新密码 */
    if (key == KEY_ENTER)
    {
        if (now - g_pwd_last_enter_tick <= APP_DOUBLE_CLICK_MS)
        {
            g_pwd_last_enter_tick = 0;
            if (g_pwd_len > 0)
            {
                /* 保存新密码 */
                memset(g_admin_password, 0, sizeof(g_admin_password));
                strncpy(g_admin_password, g_pwd_buf, APP_PWD_MAX_LEN);
                g_admin_password[APP_PWD_MAX_LEN] = '\0';
                g_pwd_result = 1;
                g_pwd_result_tick = now;
                Buzzer_On();
                HAL_Delay(30);
                Buzzer_Off();
                App_RequestImmediateTelemetry();
            }
        }
        else
        {
            g_pwd_last_enter_tick = now;
            if (g_pwd_len < APP_PWD_MAX_LEN)
            {
                g_pwd_buf[g_pwd_len] = '0' + g_pwd_digit;
                g_pwd_len++;
                g_pwd_buf[g_pwd_len] = '\0';
            }
        }
    }

    /* UP/DOWN: 数字滚动 */
    if (key == KEY_UP)
    {
        g_pwd_digit = (g_pwd_digit == 9) ? 0 : g_pwd_digit + 1;
    }
    else if (key == KEY_DOWN)
    {
        g_pwd_digit = (g_pwd_digit == 0) ? 9 : g_pwd_digit - 1;
    }

    /* 绘制 UI (标题不同) */
    OLED_ShowString(0, 0, "New Password", OLED_6X8);
    PasswordPage_DrawWheel();
}

/* ==================== 管理者模式动作 ==================== */

static void Action_ExitAdmin(void)
{
    g_admin_mode = 0;

    OLED_Clear();
    OLED_ShowString(8, 20, "Exiting Admin", OLED_8X16);
    OLED_ShowString(24, 44, "Mode...", OLED_8X16);
    OLED_Update();
    HAL_Delay(1000);

    Menu_Rebuild();
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

static void Action_DeleteAllFP(void)
{
    GZ_Empty();

    OLED_Clear();
    OLED_ShowString(4, 20, "All fingerprints", OLED_8X16);
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

static void Menu_Rebuild(void)
{
    /* 重置菜单系统 */
    MF_Reset();

    MF_Menu_t *menuMain = MF_CreateMenu("Main Menu");

    MF_Menu_t *menuFace = MF_CreateMenu("Face Manage");
    MF_Menu_t *menuRFID = MF_CreateMenu("RFID Manage");
    MF_Menu_t *menuFP = MF_CreateMenu("FP Manage");

    /* 主菜单条目 */
    MF_AddSubmenu(menuMain, "Face Manage", menuFace);
    MF_AddSubmenu(menuMain, "RFID Manage", menuRFID);
    MF_AddSubmenu(menuMain, "FP Manage", menuFP);

    /* === 管理者入口 === */
    if (g_admin_mode)
    {
        /* 管理者模式: Admin 为子菜单, 可退出和修改密码 */
        MF_Menu_t *menuAdmin = MF_CreateMenu("Admin");
        MF_AddSubmenu(menuMain, "Admin", menuAdmin);
        MF_AddAction(menuAdmin, "Exit Admin", Action_ExitAdmin);
        MF_AddCustomPage(menuAdmin, "Change PWD", Page_ChangePassword);
    }
    else
    {
        /* 普通模式: Admin 为密码输入页面 */
        MF_AddCustomPage(menuMain, "Admin", Page_PasswordInput);
    }

    /* === ESP 状态（普通和管理者模式都可见） === */
    {
        MF_Menu_t *menuESP = MF_CreateMenu("ESP Status");
        MF_AddSubmenu(menuMain, "ESP Status", menuESP);
        MF_AddCustomPage(menuESP, "Upload Log", Page_ESPUploadLog);
        MF_AddCustomPage(menuESP, "Debug Info", Page_ESPDebugInfo);
    }

    MF_AddAction(menuMain, "Cursor Style", Action_ToggleCursor);

    /* === Face 子菜单 === */
    MF_AddCustomPage(menuFace, "Verify Face", Page_FaceVerify);
    if (g_admin_mode)
    {
        MF_AddCustomPage(menuFace, "Enroll Face", Page_FaceEnroll);
        MF_Menu_t *menuFaceDel = MF_CreateMenu("Delete Face");
        MF_AddSubmenu(menuFace, "Delete Face", menuFaceDel);
        MF_AddCustomPage(menuFace, "Face Library", Page_FaceDB);
        MF_AddCustomPage(menuFaceDel, "Select Delete", Page_FaceDelete);
        MF_AddAction(menuFaceDel, "Delete All", Action_DeleteAllFaces);
    }

    /* === RFID 子菜单 === */
    MF_AddCustomPage(menuRFID, "Verify Card", Page_RFIDVerify);
    if (g_admin_mode)
    {
        MF_AddCustomPage(menuRFID, "Enroll Card", Page_RFIDEnroll);
        MF_Menu_t *menuRFIDDel = MF_CreateMenu("Delete Card");
        MF_AddSubmenu(menuRFID, "Delete Card", menuRFIDDel);
        MF_AddCustomPage(menuRFID, "Card Overview", Page_RFIDDB);
        MF_AddCustomPage(menuRFIDDel, "Select Delete", Page_RFIDDelete);
        MF_AddAction(menuRFIDDel, "Delete All", Action_DeleteAllCards);
    }

    /* === FP 子菜单 === */
    MF_AddCustomPage(menuFP, "Verify FP", Page_FPVerify);
    if (g_admin_mode)
    {
        MF_AddCustomPage(menuFP, "Enroll FP", Page_FPEnroll);
        MF_Menu_t *menuFPDel = MF_CreateMenu("Delete FP");
        MF_AddSubmenu(menuFP, "Delete FP", menuFPDel);
        MF_AddCustomPage(menuFP, "FP Info", Page_FPInfo);
        MF_AddCustomPage(menuFPDel, "Select Delete", Page_FPDelete);
        MF_AddAction(menuFPDel, "Delete All", Action_DeleteAllFP);
    }

    MF_Start(menuMain);
}

/* ==================== OneNET 云端回调 ==================== */

static void App_OnServoControl(uint8_t servo_on)
{
    if (servo_on)
    {
        App_DoorOpen();
        LED_On(GREEN_LED_GPIO_PORT, GREEN_LED_PIN);
        Buzzer_On();
        HAL_Delay(150);
        Buzzer_Off();
        g_feedback_active = 1;
        g_feedback_tick = HAL_GetTick();
    }
    else
    {
        Servo_SetAngle(APP_DOOR_CLOSE_ANGLE);
        g_door_open = 0;
    }
}

static void App_OnPasswordSet(const char *password)
{
    if (password == NULL)
    {
        return;
    }

    memset(g_admin_password, 0, sizeof(g_admin_password));
    strncpy(g_admin_password, password, APP_PWD_MAX_LEN);
    g_admin_password[APP_PWD_MAX_LEN] = '\0';
}

static uint8_t App_ReportTelemetry(void)
{
    OneNetTelemetry_t telemetry;
    uint16_t fp_valid = 0;
    uint8_t result;

    telemetry.face_count = (int32_t)g_face_count;

    GZ_ValidTempleteNum(&fp_valid);
    telemetry.fingerprint_count = (int32_t)fp_valid;

    memset(telemetry.password, 0, sizeof(telemetry.password));
    strncpy(telemetry.password, g_admin_password, sizeof(telemetry.password) - 1);

    telemetry.rfid_count = (int32_t)RC522_GetCardCount();

    telemetry.servo = g_door_open;

    result = OneNet_PublishTelemetry(&telemetry);

    /* 上传成功则记录日志 */
    if (result != 0U)
    {
        g_upload_log[g_upload_log_idx].tick = HAL_GetTick();
        g_upload_log[g_upload_log_idx].valid = 1U;
        g_upload_log_idx = (g_upload_log_idx + 1U) % ESP_LOG_MAX;
        g_upload_total_count++;
    }

    return result;
}

static void App_RequestImmediateTelemetry(void)
{
    uint32_t now = HAL_GetTick();

    g_onenet_immediate_report_pending = 1U;
    g_onenet_pending_retry_tick = now;

    if (App_ReportTelemetry() != 0U)
    {
        g_onenet_immediate_report_pending = 0U;
        g_onenet_last_report_tick = now;
    }
}

/* ==================== ESP 上传日志页面 ==================== */

static void Page_ESPUploadLog(KeyEvent_t key, uint8_t *exit_flag)
{
    uint8_t i;
    uint8_t y;
    uint8_t count = 0;

    if (key == KEY_BACK)
    {
        *exit_flag = 1;
        return;
    }

    OLED_ShowString(0, 0, "Upload Log", OLED_8X16);
    OLED_DrawLine(0, 16, 128, 16);

    /* 计算有效记录数 */
    for (i = 0; i < ESP_LOG_MAX; i++)
    {
        if (g_upload_log[i].valid)
            count++;
    }

    if (count == 0)
    {
        OLED_ShowString(4, 28, "No records yet", OLED_8X16);
        snprintf(fmt_buf, sizeof(fmt_buf), "Total: %lu", (unsigned long)g_upload_total_count);
        OLED_ShowString(4, 48, fmt_buf, OLED_6X8);
        return;
    }

    /* 从最新到最旧显示，最多5条 */
    y = 18;
    for (i = 0; i < ESP_LOG_MAX && y <= 54; i++)
    {
        /* 从最新记录开始：(idx - 1 - i + MAX) % MAX */
        uint8_t pos = (g_upload_log_idx + ESP_LOG_MAX - 1U - i) % ESP_LOG_MAX;
        if (!g_upload_log[pos].valid)
            continue;

        {
            uint32_t sec = g_upload_log[pos].tick / 1000U;
            uint32_t m = (sec / 60U) % 60U;
            uint32_t h = (sec / 3600U) % 24U;
            uint32_t s = sec % 60U;
            snprintf(fmt_buf, sizeof(fmt_buf), "#%lu  %02lu:%02lu:%02lu OK",
                     (unsigned long)(g_upload_total_count - i),
                     (unsigned long)h, (unsigned long)m, (unsigned long)s);
            OLED_ShowString(2, y, fmt_buf, OLED_6X8);
            y += 9;
        }
    }
}

/* ==================== ESP 调试信息页面 ==================== */

static void Page_ESPDebugInfo(KeyEvent_t key, uint8_t *exit_flag)
{
    if (key == KEY_BACK)
    {
        *exit_flag = 1;
        return;
    }

    OLED_ShowString(0, 0, "ESP Debug", OLED_8X16);
    OLED_DrawLine(0, 16, 128, 16);

    /* WiFi 连接状态 */
    snprintf(fmt_buf, sizeof(fmt_buf), "WiFi:  %s",
             ESP_IsWifiConnected() ? "Connected" : "Disconnected");
    OLED_ShowString(2, 18, fmt_buf, OLED_6X8);

    /* TCP 连接状态 */
    snprintf(fmt_buf, sizeof(fmt_buf), "TCP:   %s",
             ESP_IsConnected() ? "Connected" : "Disconnected");
    OLED_ShowString(2, 27, fmt_buf, OLED_6X8);

    /* MQTT 连接状态 */
    snprintf(fmt_buf, sizeof(fmt_buf), "MQTT:  %s",
             OneNet_IsConnected() ? "Connected" : "Disconnected");
    OLED_ShowString(2, 36, fmt_buf, OLED_6X8);

    /* 订阅状态 */
    snprintf(fmt_buf, sizeof(fmt_buf), "Sub:   %s",
             OneNet_IsSubscribed() ? "OK" : "Pending");
    OLED_ShowString(2, 45, fmt_buf, OLED_6X8);

    /* 上传统计 */
    snprintf(fmt_buf, sizeof(fmt_buf), "Uploads: %lu",
             (unsigned long)g_upload_total_count);
    OLED_ShowString(2, 54, fmt_buf, OLED_6X8);
}

void App_Init(void)
{
    HAL_Init();

    sys_stm32_clock_init(RCC_PLL_MUL9);
    delay_init(72);

    OLED_Init();
    Key_Init();
    Buzzer_Init();
    LED_Init();
    Servo_Init();
    RC522_Init();
    AS608_Init();
    FM225_Init();

    Servo_SetAngle(APP_DOOR_CLOSE_ANGLE);

    LED_Off(RED_LED_GPIO_PORT, RED_LED_PIN);
    LED_Off(GREEN_LED_GPIO_PORT, GREEN_LED_PIN);
    Buzzer_Off();

    FM225_SetMatchedCallback(OnFaceMatched);
    FM225_SetUnmatchedCallback(OnFaceUnmatched);
    FM225_SetInvalidCallback(OnFaceInvalid);
    FM225_SetEnrollDoneCallback(OnEnrollDone);
    FM225_SetEnrollFailCallback(OnEnrollFail);
    FM225_SetFaceCountCallback(OnFaceCount);

    /* OneNET 云平台初始化 */
    OneNet_Init();
    OneNet_RegisterServoControlCallback(App_OnServoControl);
    OneNet_RegisterPasswordSetCallback(App_OnPasswordSet);
    g_onenet_last_report_tick = HAL_GetTick();
    g_onenet_pending_retry_tick = g_onenet_last_report_tick;

    Menu_Rebuild();

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
    FM225_Process();

    /* ---- OneNET MQTT 状态机驱动 ---- */
    OneNet_Process();

    App_FeedbackUpdate();

    MF_CustomPageCb cur_page = MF_GetCustomPageCb();

    /* ---- AS608 WAK 自动触发并跳转到指纹验证页 ---- */
    if (!g_fp_auto_detected && g_fp_state == 0 && g_verify_state != VERIFY_WAITING && g_enroll_state == ENROLL_IDLE && g_rfid_state == 0 && cur_page != Page_FPVerify && cur_page != Page_FPEnroll && cur_page != Page_FPDelete && cur_page != Page_RFIDVerify && cur_page != Page_RFIDEnroll && cur_page != Page_FaceVerify && cur_page != Page_FaceEnroll && cur_page != Page_PasswordInput && cur_page != Page_ChangePassword)
    {
        if (HAL_GPIO_ReadPin(AS608_GPIO_PORT, AS608_WAK_PIN) == GPIO_PIN_SET)
        {
            g_fp_auto_detected = 1;
            g_fp_state = 0;
            MF_EnterCustomPage(Page_FPVerify);
        }
    }
    else if (g_fp_auto_detected && g_fp_state == 0 && cur_page != Page_FPVerify)
    {
        g_fp_auto_detected = 0;
    }

    /* ---- RFID 自动检测 ---- */
    if (g_verify_state != VERIFY_WAITING && g_enroll_state == ENROLL_IDLE && g_rfid_state == 0 && !g_rfid_auto_detected && !g_fp_auto_detected && cur_page != Page_RFIDEnroll && cur_page != Page_RFIDDelete && cur_page != Page_RFIDVerify && cur_page != Page_FaceVerify && cur_page != Page_FaceEnroll && cur_page != Page_FPVerify && cur_page != Page_FPEnroll && cur_page != Page_PasswordInput && cur_page != Page_ChangePassword)
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

    /* ---- OneNET 待补发优先处理 ---- */
    if (g_onenet_immediate_report_pending != 0U)
    {
        uint32_t now = HAL_GetTick();
        if ((OneNet_IsConnected() != 0U) &&
            ((now - g_onenet_pending_retry_tick) >= APP_ONENET_PENDING_RETRY_MS))
        {
            g_onenet_pending_retry_tick = now;
            if (App_ReportTelemetry() != 0U)
            {
                g_onenet_immediate_report_pending = 0U;
                g_onenet_last_report_tick = now;
            }
        }
    }

    /* ---- OneNET 定时上报物模型数据 ---- */
    {
        uint32_t now = HAL_GetTick();
        if ((now - g_onenet_last_report_tick) >= APP_ONENET_REPORT_INTERVAL_MS)
        {
            g_onenet_last_report_tick = now;
            if (App_ReportTelemetry() != 0U)
            {
                g_onenet_immediate_report_pending = 0U;
            }
        }
    }

    MF_Loop();

    App_IWDG_Feed();
}
