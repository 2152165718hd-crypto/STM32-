#include ".\Application\Menu\Menu.h"
#include <stdio.h>
#include <string.h>

/* =====================================================================
 *  全局状态变量
 * ===================================================================== */

/* 执行器状态 */
static uint8_t buzzer_state = 0;   /* 0=OFF, 1=ON */
static uint8_t relay_state  = 0;   /* 0=OFF (锁关), 1=ON (锁开) */

/* 蜂鸣器非阻塞报警 */
static uint8_t  buzzer_alarm_active = 0;
static uint32_t buzzer_alarm_start  = 0;
#define BUZZER_ALARM_DURATION_MS 3000

/* 人脸模块状态 */
static uint8_t  face_count = 0;

/* 人脸识别结果 */
typedef enum {
    VERIFY_IDLE = 0,
    VERIFY_WAITING,
    VERIFY_SUCCESS,
    VERIFY_FAIL_NOMATCH,
    VERIFY_FAIL_ERROR
} VerifyState_t;
static VerifyState_t verify_state = VERIFY_IDLE;
static char     matched_name[33]  = {0};
static int16_t  matched_id        = -1;
static uint8_t  verify_error_code = 0;

/* 人脸录入结果 */
typedef enum {
    ENROLL_IDLE = 0,
    ENROLL_WAITING,
    ENROLL_SUCCESS,
    ENROLL_FAIL
} EnrollState_t;
static EnrollState_t enroll_state = ENROLL_IDLE;
static int16_t  enrolled_id       = -1;
static uint8_t  enroll_error_code = 0;

/* 多步录入：方向序列与当前步骤 */
static const FM225_FaceDirection_t enroll_dir_seq[] = {
    FM225_DIR_MIDDLE, FM225_DIR_UP, FM225_DIR_DOWN,
    FM225_DIR_LEFT,   FM225_DIR_RIGHT
};
static const char *enroll_dir_name[] = {
    "Middle", "Up", "Down", "Left", "Right"
};
#define ENROLL_STEP_COUNT  5
static uint8_t enroll_step = 0;         /* 当前方向步骤 0..4 */
static char    enroll_name_buf[32] = {0}; /* 保存姓名供后续步骤复用 */

/* 删除操作状态 */
typedef enum {
    DEL_IDLE = 0,
    DEL_WAITING,
    DEL_DONE
} DeleteState_t;
static DeleteState_t delete_state = DEL_IDLE;
static int32_t  delete_face_id    = 0; /* 用于数值编辑条目 */

/* ESP WiFi 状态 */
static uint8_t esp_init_ok    = 0;   /* ESP 初始化是否成功 */
static uint8_t esp_connected  = 0;   /* 是否有客户端连接 */
static uint8_t esp_link_id    = 0;   /* 活动连接号 */

/* ESP 日志记录 */
#define LOG_ENTRY_SIZE 20
#define LOG_MAX_ENTRIES 5

static char esp_rx_log[LOG_MAX_ENTRIES][LOG_ENTRY_SIZE]; /* 接收指令记录 */
static uint8_t esp_rx_log_count = 0;
static uint8_t esp_rx_log_index = 0;  /* 下一条写入位置 */

static char esp_tx_log[LOG_MAX_ENTRIES][LOG_ENTRY_SIZE]; /* 上传数据记录 */
static uint8_t esp_tx_log_count = 0;
static uint8_t esp_tx_log_index = 0;

/* 显示缓冲 */
static char disp_buf[48];

/* =====================================================================
 *  日志工具函数
 * ===================================================================== */

static void Log_AddRx(const char *entry)
{
    strncpy(esp_rx_log[esp_rx_log_index], entry, LOG_ENTRY_SIZE - 1);
    esp_rx_log[esp_rx_log_index][LOG_ENTRY_SIZE - 1] = '\0';
    esp_rx_log_index = (esp_rx_log_index + 1) % LOG_MAX_ENTRIES;
    if (esp_rx_log_count < LOG_MAX_ENTRIES)
        esp_rx_log_count++;
}

static void Log_AddTx(const char *entry)
{
    strncpy(esp_tx_log[esp_tx_log_index], entry, LOG_ENTRY_SIZE - 1);
    esp_tx_log[esp_tx_log_index][LOG_ENTRY_SIZE - 1] = '\0';
    esp_tx_log_index = (esp_tx_log_index + 1) % LOG_MAX_ENTRIES;
    if (esp_tx_log_count < LOG_MAX_ENTRIES)
        esp_tx_log_count++;
}

/* 向 ESP 发送状态并记录日志 */
static void ESP_SendAndLog(uint8_t link_id, const char *msg)
{
    ESP_SendString(link_id, msg);
    Log_AddTx(msg);
}

/* =====================================================================
 *  图形绘制辅助函数 — 参考开源项目界面设计
 * ===================================================================== */

/**
 * @brief  绘制成功（对勾）图标: 圆圈 + 勾
 * @param  cx,cy  圆心坐标
 * @param  r      半径
 */
static void Draw_IconSuccess(int16_t cx, int16_t cy, uint8_t r)
{
    OLED_DrawCircle(cx, cy, r, OLED_UNFILLED);
    /* 对勾: 从左下到中底，再到右上 */
    int16_t x1 = cx - (int16_t)(r * 4 / 10);  /* 勾起点 */
    int16_t y1 = cy + 1;
    int16_t x2 = cx - (int16_t)(r * 1 / 10);  /* 勾拐点 */
    int16_t y2 = cy + (int16_t)(r * 4 / 10);
    int16_t x3 = cx + (int16_t)(r * 4 / 10);  /* 勾终点 */
    int16_t y3 = cy - (int16_t)(r * 3 / 10);
    OLED_DrawLine(x1, y1, x2, y2);
    OLED_DrawLine(x2, y2, x3, y3);
}

/**
 * @brief  绘制失败（叉号）图标: 圆圈 + X
 * @param  cx,cy  圆心坐标
 * @param  r      半径
 */
static void Draw_IconFail(int16_t cx, int16_t cy, uint8_t r)
{
    OLED_DrawCircle(cx, cy, r, OLED_UNFILLED);
    /* X 叉号，偏移 r*0.5 */
    int16_t d = (int16_t)(r * 5 / 10);
    OLED_DrawLine(cx - d, cy - d, cx + d, cy + d);
    OLED_DrawLine(cx - d, cy + d, cx + d, cy - d);
}

/**
 * @brief  绘制锁定图标（锁环弧 + 锁体矩形）
 * @param  x,y  左上角坐标
 *         整体约 16x20 像素
 */
static void Draw_IconLock(int16_t x, int16_t y)
{
    /* 锁环：上方半圆弧, 半径5 */
    OLED_DrawArc(x + 8, y + 6, 5, 180, 360, OLED_UNFILLED);
    /* 锁环两侧竖线 */
    OLED_DrawLine(x + 3, y + 6, x + 3, y + 9);
    OLED_DrawLine(x + 13, y + 6, x + 13, y + 9);
    /* 锁体：矩形 */
    OLED_DrawRectangle(x + 1, y + 9, 14, 10, OLED_UNFILLED);
    /* 锁孔：小圆点 */
    OLED_DrawCircle(x + 8, y + 13, 1, OLED_FILLED);
    /* 锁孔下竖线 */
    OLED_DrawLine(x + 8, y + 14, x + 8, y + 16);
}

/**
 * @brief  绘制开锁图标（锁环偏移 + 锁体矩形）
 * @param  x,y  左上角坐标
 */
static void Draw_IconUnlock(int16_t x, int16_t y)
{
    /* 锁环：上方半圆弧偏右上 */
    OLED_DrawArc(x + 8, y + 4, 5, 180, 360, OLED_UNFILLED);
    /* 只画右侧竖线连到锁体，左侧断开表示开锁 */
    OLED_DrawLine(x + 13, y + 4, x + 13, y + 9);
    /* 锁体 */
    OLED_DrawRectangle(x + 1, y + 9, 14, 10, OLED_UNFILLED);
    /* 锁孔 */
    OLED_DrawCircle(x + 8, y + 13, 1, OLED_FILLED);
}

/**
 * @brief  绘制滚动省略号动画 (".", "..", "...")
 * @param  x,y  起始坐标
 * @param  font 字体大小
 */
static void Draw_ProgressDots(int16_t x, int16_t y, uint8_t font)
{
    uint32_t phase = (HAL_GetTick() / 500) % 4; /* 0,1,2,3 -> "",".","..","..." */
    if (phase >= 1) OLED_ShowChar(x,      y, '.', font);
    if (phase >= 2) OLED_ShowChar(x + 6,  y, '.', font);
    if (phase >= 3) OLED_ShowChar(x + 12, y, '.', font);
}

/**
 * @brief  绘制人脸扫描框动画
 *         四角标记 + 水平扫描线从上到下循环
 * @param  x,y  左上角
 * @param  w,h  宽高
 */
static void Draw_ScanFrame(int16_t x, int16_t y, uint8_t w, uint8_t h)
{
    uint8_t corner = 6; /* 角标长度 */

    /* 左上角 */
    OLED_DrawLine(x, y, x + corner, y);
    OLED_DrawLine(x, y, x, y + corner);
    /* 右上角 */
    OLED_DrawLine(x + w - 1, y, x + w - 1 - corner, y);
    OLED_DrawLine(x + w - 1, y, x + w - 1, y + corner);
    /* 左下角 */
    OLED_DrawLine(x, y + h - 1, x + corner, y + h - 1);
    OLED_DrawLine(x, y + h - 1, x, y + h - 1 - corner);
    /* 右下角 */
    OLED_DrawLine(x + w - 1, y + h - 1, x + w - 1 - corner, y + h - 1);
    OLED_DrawLine(x + w - 1, y + h - 1, x + w - 1, y + h - 1 - corner);

    /* 水平扫描线动画: 从上到下循环 */
    {
        uint32_t scan_pos = (HAL_GetTick() / 30) % (uint32_t)(h - 4);
        int16_t scan_y = y + 2 + (int16_t)scan_pos;
        OLED_DrawLine(x + 2, scan_y, x + w - 3, scan_y);
    }
}

/**
 * @brief  绘制警告三角图标
 * @param  cx,cy  中心坐标
 * @param  size   三角半高
 */
static void Draw_IconWarning(int16_t cx, int16_t cy, uint8_t size)
{
    /* 等腰三角形: 顶点在上，底边在下 */
    int16_t top_x = cx;
    int16_t top_y = cy - size;
    int16_t bl_x  = cx - size;
    int16_t bl_y  = cy + size;
    int16_t br_x  = cx + size;
    int16_t br_y  = cy + size;
    OLED_DrawTriangle(top_x, top_y, bl_x, bl_y, br_x, br_y, OLED_UNFILLED);
    /* 感叹号 */
    OLED_DrawLine(cx, cy - size + 4, cx, cy + 2);
    OLED_DrawPoint(cx, cy + size - 3);
}

/**
 * @brief  绘制数据库图标（档案柜造型）
 * @param  x,y  左上角
 */
static void Draw_IconDatabase(int16_t x, int16_t y)
{
    /* 外框: 12x16 矩形 */
    OLED_DrawRectangle(x, y, 12, 16, OLED_UNFILLED);
    /* 三层横线分割 */
    OLED_DrawLine(x, y + 5,  x + 11, y + 5);
    OLED_DrawLine(x, y + 10, x + 11, y + 10);
    /* 每层一个小圆点（把手） */
    OLED_DrawPoint(x + 6, y + 2);
    OLED_DrawPoint(x + 6, y + 7);
    OLED_DrawPoint(x + 6, y + 12);
}

/* =====================================================================
 *  蜂鸣器非阻塞报警管理
 * ===================================================================== */

static void Buzzer_StartAlarm(void)
{
    Buzzer_On();
    buzzer_state = 1;
    buzzer_alarm_active = 1;
    buzzer_alarm_start  = HAL_GetTick();
}

static void Buzzer_AlarmTick(void)
{
    if (buzzer_alarm_active)
    {
        if ((HAL_GetTick() - buzzer_alarm_start) >= BUZZER_ALARM_DURATION_MS)
        {
            Buzzer_Off();
            buzzer_state = 0;
            buzzer_alarm_active = 0;
        }
    }
}

/* =====================================================================
 *  FM225 回调函数
 * ===================================================================== */

/* 识别成功 */
static void OnFaceMatched(int16_t face_id, const char *name)
{
    verify_state = VERIFY_SUCCESS;
    matched_id   = face_id;
    strncpy(matched_name, name, 32);
    matched_name[32] = '\0';

    /* 开锁 */
    Relay_On();
    relay_state = 1;
    
    /* 上报状态及识别记录 */
    if (esp_connected)
    {
        snprintf(disp_buf, sizeof(disp_buf), "LOCK:OPEN,ID:%d,NAME:%s", face_id, name);
        ESP_SendAndLog(esp_link_id, disp_buf);
    }
}

/* 识别失败 — 已注册人脸但不匹配 */
static void OnFaceUnmatched(void)
{
    verify_state = VERIFY_FAIL_NOMATCH;

    /* 蜂鸣器报警 3 秒 */
    Buzzer_StartAlarm();
    
    /* 上报报警记录 */
    if (esp_connected)
    {
        ESP_SendAndLog(esp_link_id, "WARN:UNMATCHED");
    }
}

/* 识别错误/超时 */
static void OnFaceInvalid(uint8_t error)
{
    verify_state    = VERIFY_FAIL_ERROR;
    verify_error_code = error;
    
    /* 蜂鸣器报警 3 秒 */
    Buzzer_StartAlarm();
}

/* 录入完成 —— face_id==-1 表示当前方向步骤完成但未结束，需继续下一步 */
static void OnEnrollDone(int16_t face_id, uint8_t direction)
{
    (void)direction;
    if (face_id == -1)
    {
        /* 当前方向步骤完成，推进到下一个方向 */
        enroll_step++;
        if (enroll_step < ENROLL_STEP_COUNT)
        {
            FM225_EnrollFace(enroll_name_buf, enroll_dir_seq[enroll_step]);
        }
        /* 如果已超过最大步骤但仍返回 -1，等待模块超时或后续响应 */
    }
    else
    {
        /* 真正录入成功，返回有效 face_id */
        enroll_state = ENROLL_SUCCESS;
        enrolled_id  = face_id;
        enroll_step  = 0;
    }
}

/* 录入失败 */
static void OnEnrollFail(uint8_t error)
{
    enroll_state      = ENROLL_FAIL;
    enroll_error_code = error;
    enroll_step       = 0;
}

/* 人脸数量更新 */
static void OnFaceCount(uint8_t count)
{
    face_count = count;
}

/* =====================================================================
 *  ESP 数据接收回调
 * ===================================================================== */

void Menu_ESP_DataCallback(ESP_DataPacket_t *packet)
{
    if (packet == NULL || packet->len == 0)
        return;

    esp_connected = 1;
    esp_link_id   = packet->link_id;

    /* 提取指令文本（截断到缓冲区大小） */
    char cmd[LOG_ENTRY_SIZE];
    uint16_t copy_len = packet->len;
    if (copy_len > LOG_ENTRY_SIZE - 1)
        copy_len = LOG_ENTRY_SIZE - 1;
    memcpy(cmd, packet->data, copy_len);
    cmd[copy_len] = '\0';

    /* 去除尾部回车换行 */
    while (copy_len > 0 && (cmd[copy_len - 1] == '\r' || cmd[copy_len - 1] == '\n'))
    {
        cmd[--copy_len] = '\0';
    }

    /* 记录日志 */
    Log_AddRx(cmd);

    /* 解析指令 */
    if (strcmp(cmd, "OPEN") == 0)
    {
        Relay_On();
        relay_state = 1;
        ESP_SendAndLog(packet->link_id, "LOCK:OPEN");
    }
    else if (strcmp(cmd, "CLOSE") == 0)
    {
        Relay_Off();
        relay_state = 0;
        ESP_SendAndLog(packet->link_id, "LOCK:CLOSE");
    }
    else if (strcmp(cmd, "STATUS") == 0)
    {
        const char *lock_str = relay_state ? "OPEN" : "CLOSE";
        snprintf(disp_buf, sizeof(disp_buf), "LOCK:%s,FACE:%d", lock_str, face_count);
        ESP_SendAndLog(packet->link_id, disp_buf);
    }
    else
    {
        ESP_SendAndLog(packet->link_id, "ERR:UNKNOWN");
    }
}

/* =====================================================================
 *  自定义页面回调 — 人脸识别
 * ===================================================================== */

static void Page_FaceVerify(KeyEvent_t key, uint8_t *exit_flag)
{
    /* 首次进入自动触发识别 */
    if (verify_state == VERIFY_IDLE)
    {
        verify_state = VERIFY_WAITING;
        FM225_VerifyFace();  /* 内部会自动抢占后台初始化查询 */
    }

    /* BACK 键退出 */
    if (key == KEY_BACK)
    {
        FM225_CancelCurrentTask();
        verify_state = VERIFY_IDLE;
        *exit_flag = 1;
        return;
    }

    /* 识别成功后按 ENTER 关锁并返回 */
    if (verify_state == VERIFY_SUCCESS && key == KEY_ENTER)
    {
        Relay_Off();
        relay_state = 0;
        verify_state = VERIFY_IDLE;
        if (esp_connected)
        {
            ESP_SendAndLog(esp_link_id, "LOCK:CLOSE");
        }
        *exit_flag = 1;
        return;
    }

    /* 失败后按 ENTER 重新识别 */
    if ((verify_state == VERIFY_FAIL_NOMATCH || verify_state == VERIFY_FAIL_ERROR) && key == KEY_ENTER)
    {
        verify_state = VERIFY_WAITING;
        FM225_VerifyFace();
    }

    /* ---- 渲染 ---- */
    OLED_Clear();
    OLED_ShowString(28, 0, "Face Verify", OLED_8X16);
    OLED_DrawLine(0, 16, 128, 16);

    switch (verify_state)
    {
    case VERIFY_IDLE:
        /* 等待模块就绪 */
        Draw_IconLock(56, 22);
        OLED_ShowString(20, 46, "Preparing", OLED_6X8);
        Draw_ProgressDots(74, 46, OLED_6X8);
        break;

    case VERIFY_WAITING:
        /* 人脸扫描框动画（居中） */
        Draw_ScanFrame(40, 20, 48, 34);
        /* 扫描框左侧文字 */
        OLED_ShowString(0, 22, "Scan", OLED_6X8);
        /* 右侧滚动省略号 */
        Draw_ProgressDots(92, 22, OLED_6X8);
        /* 底部提示 */
        OLED_ShowString(12, 56, "Please face cam", OLED_6X8);
        break;

    case VERIFY_SUCCESS:
        /* 左侧: 对勾图标 */
        Draw_IconSuccess(20, 32, 10);
        /* 右侧: 开锁图标 */
        Draw_IconUnlock(104, 22);
        /* 中间文字 */
        OLED_ShowString(38, 22, "Pass!", OLED_8X16);
        snprintf(disp_buf, sizeof(disp_buf), "ID:%d %s", matched_id, matched_name);
        OLED_ShowString(34, 40, disp_buf, OLED_6X8);
        /* 底部提示 */
        OLED_ShowString(4, 56, "OK:Lock  BACK:Ret", OLED_6X8);
        break;

    case VERIFY_FAIL_NOMATCH:
        /* 叉号图标 */
        Draw_IconFail(20, 32, 10);
        /* 锁定图标 */
        Draw_IconLock(104, 22);
        /* 文字 */
        OLED_ShowString(38, 22, "Denied!", OLED_8X16);
        OLED_ShowString(38, 40, "Not recognized", OLED_6X8);
        /* 底部提示 */
        OLED_ShowString(4, 56, "OK:Retry BACK:Ret", OLED_6X8);
        break;

    case VERIFY_FAIL_ERROR:
        /* 叉号图标 */
        Draw_IconFail(20, 32, 10);
        /* 文字 */
        OLED_ShowString(38, 22, "Error!", OLED_8X16);
        snprintf(disp_buf, sizeof(disp_buf), "Code: 0x%02X", verify_error_code);
        OLED_ShowString(38, 40, disp_buf, OLED_6X8);
        /* 底部提示 */
        OLED_ShowString(4, 56, "OK:Retry BACK:Ret", OLED_6X8);
        break;

    default:
        break;
    }

    OLED_Update();
}

/* =====================================================================
 *  自定义页面回调 — 人脸录入
 * ===================================================================== */

static void Page_FaceEnroll(KeyEvent_t key, uint8_t *exit_flag)
{
    /* 首次进入自动触发录入（第1步：Middle） */
    if (enroll_state == ENROLL_IDLE)
    {
        enroll_state = ENROLL_WAITING;
        enroll_step  = 0;
        snprintf(enroll_name_buf, sizeof(enroll_name_buf), "User%02d", face_count + 1);
        FM225_EnrollFace(enroll_name_buf, enroll_dir_seq[0]);
    }

    if (key == KEY_BACK)
    {
        FM225_CancelCurrentTask();
        enroll_state = ENROLL_IDLE;
        enroll_step  = 0;
        *exit_flag = 1;
        return;
    }

    /* 完成后按 ENTER 重新录入 */
    if ((enroll_state == ENROLL_SUCCESS || enroll_state == ENROLL_FAIL) && key == KEY_ENTER)
    {
        enroll_state = ENROLL_WAITING;
        enroll_step  = 0;
        snprintf(enroll_name_buf, sizeof(enroll_name_buf), "User%02d", face_count + 1);
        FM225_EnrollFace(enroll_name_buf, enroll_dir_seq[0]);
    }

    /* ---- 渲染 ---- */
    OLED_Clear();
    OLED_ShowString(24, 0, "Face Enroll", OLED_8X16);
    OLED_DrawLine(0, 16, 128, 16);

    switch (enroll_state)
    {
    case ENROLL_IDLE:
        /* 等待模块就绪 */
        Draw_IconLock(56, 22);
        OLED_ShowString(20, 46, "Preparing", OLED_6X8);
        Draw_ProgressDots(74, 46, OLED_6X8);
        break;

    case ENROLL_WAITING:
        /* 扫描框动画 */
        Draw_ScanFrame(40, 20, 48, 34);
        /* 左侧显示当前步骤/方向 */
        snprintf(disp_buf, sizeof(disp_buf), "%d/%d", enroll_step + 1, ENROLL_STEP_COUNT);
        OLED_ShowString(0, 22, disp_buf, OLED_6X8);
        /* 右侧显示方向名称 */
        if (enroll_step < ENROLL_STEP_COUNT)
            OLED_ShowString(92, 22, (char *)enroll_dir_name[enroll_step], OLED_6X8);
        Draw_ProgressDots(92, 32, OLED_6X8);
        /* 底部提示 */
        OLED_ShowString(4, 56, "Face cam & follow", OLED_6X8);
        break;

    case ENROLL_SUCCESS:
        /* 对勾图标 */
        Draw_IconSuccess(20, 32, 10);
        /* 文字 */
        OLED_ShowString(38, 22, "Enrolled!", OLED_8X16);
        snprintf(disp_buf, sizeof(disp_buf), "Face ID: %d", enrolled_id);
        OLED_ShowString(38, 40, disp_buf, OLED_6X8);
        /* 底部提示 */
        OLED_ShowString(4, 56, "OK:More  BACK:Ret", OLED_6X8);
        break;

    case ENROLL_FAIL:
        /* 叉号图标 */
        Draw_IconFail(20, 32, 10);
        /* 文字 */
        OLED_ShowString(38, 22, "Failed!", OLED_8X16);
        snprintf(disp_buf, sizeof(disp_buf), "Err: 0x%02X", enroll_error_code);
        OLED_ShowString(38, 40, disp_buf, OLED_6X8);
        /* 底部提示 */
        OLED_ShowString(4, 56, "OK:Retry BACK:Ret", OLED_6X8);
        break;

    default:
        break;
    }

    OLED_Update();
}

/* =====================================================================
 *  自定义页面回调 — 删除指定人脸
 * ===================================================================== */

static void Page_DeleteOne(KeyEvent_t key, uint8_t *exit_flag)
{
    if (key == KEY_BACK)
    {
        delete_state = DEL_IDLE;
        *exit_flag = 1;
        return;
    }

    if (delete_state == DEL_IDLE || delete_state == DEL_DONE)
    {
        /* 上下键调整 ID */
        if (key == KEY_UP)
        {
            delete_face_id++;
            if (delete_face_id > 99)
                delete_face_id = 99;
        }
        else if (key == KEY_DOWN)
        {
            delete_face_id--;
            if (delete_face_id < 0)
                delete_face_id = 0;
        }
        else if (key == KEY_ENTER && delete_state != DEL_WAITING)
        {
            delete_state = DEL_WAITING;
            FM225_DeleteFace((int16_t)delete_face_id);
            delete_state = DEL_DONE;
            /* 人脸数量由 FM225 HandleReply 自动延迟刷新 */
        }
    }

    /* ---- 渲染 ---- */
    OLED_Clear();
    OLED_ShowString(16, 0, "Delete Face", OLED_8X16);
    OLED_DrawLine(0, 16, 128, 16);

    if (delete_state == DEL_DONE)
    {
        /* 删除成功反馈: 对勾图标 */
        Draw_IconSuccess(20, 34, 10);
        OLED_ShowString(38, 26, "Deleted!", OLED_8X16);
        snprintf(disp_buf, sizeof(disp_buf), "ID: %ld", (long)delete_face_id);
        OLED_ShowString(38, 44, disp_buf, OLED_6X8);
    }
    else
    {
        /* 人脸小图标装饰 */
        OLED_DrawCircle(12, 28, 5, OLED_UNFILLED);  /* 头部 */
        OLED_DrawArc(12, 42, 8, 220, 320, OLED_UNFILLED); /* 肩膀 */

        /* ID 选择 */
        snprintf(disp_buf, sizeof(disp_buf), "ID: < %ld >", (long)delete_face_id);
        OLED_ShowString(26, 22, disp_buf, OLED_8X16);

        /* 上下箭头指示 */
        OLED_DrawTriangle(114, 20, 110, 26, 118, 26, OLED_UNFILLED); /* 上箭头 */
        OLED_DrawTriangle(114, 40, 110, 34, 118, 34, OLED_UNFILLED); /* 下箭头 */

        OLED_ShowString(26, 42, "OK:Delete", OLED_6X8);
    }

    OLED_ShowString(4, 56, "BACK: Return", OLED_6X8);

    OLED_Update();
}

/* =====================================================================
 *  自定义页面回调 — 删除全部人脸
 * ===================================================================== */

static uint8_t delete_all_confirm = 0;

static void Page_DeleteAll(KeyEvent_t key, uint8_t *exit_flag)
{
    if (key == KEY_BACK)
    {
        delete_all_confirm = 0;
        *exit_flag = 1;
        return;
    }

    OLED_Clear();
    OLED_ShowString(12, 0, "Delete All", OLED_8X16);
    OLED_DrawLine(0, 16, 128, 16);

    if (delete_all_confirm == 0)
    {
        /* 警告三角图标 */
        Draw_IconWarning(20, 34, 10);

        /* 警告文字 */
        OLED_ShowString(36, 22, "Delete ALL?", OLED_8X16);
        snprintf(disp_buf, sizeof(disp_buf), "Total: %d face(s)", face_count);
        OLED_ShowString(36, 40, disp_buf, OLED_6X8);

        /* 底部操作提示 */
        OLED_ShowString(4, 56, "OK:Confirm BACK:No", OLED_6X8);

        if (key == KEY_ENTER)
        {
            FM225_DeleteAllFaces();
            /* 人脸数量由 FM225 HandleReply 自动延迟刷新 */
            delete_all_confirm = 1;
        }
    }
    else
    {
        /* 成功对勾图标 */
        Draw_IconSuccess(20, 34, 10);

        OLED_ShowString(38, 26, "All Clear!", OLED_8X16);
        OLED_ShowString(38, 44, "Database empty", OLED_6X8);
        OLED_ShowString(16, 56, "BACK: Return", OLED_6X8);
    }

    OLED_Update();
}

/* =====================================================================
 *  自定义页面回调 — 人脸库信息
 * ===================================================================== */

static void Page_FaceInfo(KeyEvent_t key, uint8_t *exit_flag)
{
    if (key == KEY_BACK)
    {
        *exit_flag = 1;
        return;
    }

    /* 按 ENTER 刷新 */
    if (key == KEY_ENTER)
    {
        FM225_GetFaceCount();
    }

    OLED_Clear();
    OLED_ShowString(16, 0, "Face Database", OLED_8X16);
    OLED_DrawLine(0, 16, 128, 16);

    /* 数据库图标 */
    Draw_IconDatabase(4, 20);

    /* 人脸数量 */
    snprintf(disp_buf, sizeof(disp_buf), "Faces: %d", face_count);
    OLED_ShowString(22, 22, disp_buf, OLED_8X16);

    /* 锁状态图标 + 文字 */
    if (relay_state)
    {
        Draw_IconUnlock(4, 40);
        OLED_ShowString(22, 42, "Lock: OPEN", OLED_6X8);
    }
    else
    {
        Draw_IconLock(4, 40);
        OLED_ShowString(22, 42, "Lock: CLOSED", OLED_6X8);
    }

    /* 分隔装饰线 */
    OLED_DrawLine(0, 54, 128, 54);
    OLED_ShowString(4, 56, "OK:Refresh BACK:Ret", OLED_6X8);

    OLED_Update();
}

/* =====================================================================
 *  自定义页面回调 — ESP01S 状态信息
 * ===================================================================== */

static void Page_ESPStatus(KeyEvent_t key, uint8_t *exit_flag)
{
    if (key == KEY_BACK)
    {
        *exit_flag = 1;
        return;
    }

    OLED_Clear();
    OLED_ShowString(16, 0, "ESP01S Info", OLED_8X16);
    OLED_DrawLine(0, 16, 128, 16);

    /* 模块初始化状态 */
    snprintf(disp_buf, sizeof(disp_buf), "Init: %s", esp_init_ok ? "OK" : "FAIL");
    OLED_ShowString(4, 18, disp_buf, OLED_8X16);

    /* 客户端连接状态 */
    snprintf(disp_buf, sizeof(disp_buf), "Client: %s", esp_connected ? "YES" : "NO");
    OLED_ShowString(4, 34, disp_buf, OLED_8X16);

    /* WiFi 信息 */
    snprintf(disp_buf, sizeof(disp_buf), "AP:%s", ESP_WIFI_SSID);
    OLED_ShowString(4, 50, disp_buf, OLED_6X8);

    snprintf(disp_buf, sizeof(disp_buf), "Port:%s", ESP_SERVER_PORT);
    OLED_ShowString(76, 50, disp_buf, OLED_6X8);

    OLED_Update();
}

/* =====================================================================
 *  自定义页面回调 — 接收指令记录
 * ===================================================================== */

static uint8_t rx_log_scroll = 0;

static void Page_ESPRxLog(KeyEvent_t key, uint8_t *exit_flag)
{
    if (key == KEY_BACK)
    {
        rx_log_scroll = 0;
        *exit_flag = 1;
        return;
    }

    if (key == KEY_DOWN && rx_log_scroll < LOG_MAX_ENTRIES - 3)
        rx_log_scroll++;
    if (key == KEY_UP && rx_log_scroll > 0)
        rx_log_scroll--;

    OLED_Clear();
    OLED_ShowString(16, 0, "RX CMD Log", OLED_8X16);
    OLED_DrawLine(0, 16, 128, 16);

    if (esp_rx_log_count == 0)
    {
        OLED_ShowString(16, 28, "No records", OLED_8X16);
    }
    else
    {
        /* 显示最多3条（受屏幕高度限制） */
        uint8_t i;
        for (i = 0; i < 3; i++)
        {
            uint8_t idx = rx_log_scroll + i;
            if (idx >= esp_rx_log_count)
                break;

            /* 从最旧到最新排列:
             * 环形缓冲区起始位置 = (rx_log_index - rx_log_count + MAX) % MAX */
            uint8_t real_idx = (esp_rx_log_index - esp_rx_log_count + idx + LOG_MAX_ENTRIES) % LOG_MAX_ENTRIES;

            snprintf(disp_buf, sizeof(disp_buf), "%d:%s", idx + 1, esp_rx_log[real_idx]);
            OLED_ShowString(0, 18 + i * 14, disp_buf, OLED_6X8);
        }
    }

    /* 滚动提示 */
    if (esp_rx_log_count > 3)
    {
        if (rx_log_scroll > 0)
            OLED_ShowChar(120, 18, '^', OLED_6X8);
        if (rx_log_scroll + 3 < esp_rx_log_count)
            OLED_ShowChar(120, 50, 'v', OLED_6X8);
    }

    OLED_Update();
}

/* =====================================================================
 *  自定义页面回调 — 上传数据记录
 * ===================================================================== */

static uint8_t tx_log_scroll = 0;

static void Page_ESPTxLog(KeyEvent_t key, uint8_t *exit_flag)
{
    if (key == KEY_BACK)
    {
        tx_log_scroll = 0;
        *exit_flag = 1;
        return;
    }

    if (key == KEY_DOWN && tx_log_scroll < LOG_MAX_ENTRIES - 3)
        tx_log_scroll++;
    if (key == KEY_UP && tx_log_scroll > 0)
        tx_log_scroll--;

    OLED_Clear();
    OLED_ShowString(12, 0, "TX Data Log", OLED_8X16);
    OLED_DrawLine(0, 16, 128, 16);

    if (esp_tx_log_count == 0)
    {
        OLED_ShowString(16, 28, "No records", OLED_8X16);
    }
    else
    {
        uint8_t i;
        for (i = 0; i < 3; i++)
        {
            uint8_t idx = tx_log_scroll + i;
            if (idx >= esp_tx_log_count)
                break;

            uint8_t real_idx = (esp_tx_log_index - esp_tx_log_count + idx + LOG_MAX_ENTRIES) % LOG_MAX_ENTRIES;

            snprintf(disp_buf, sizeof(disp_buf), "%d:%s", idx + 1, esp_tx_log[real_idx]);
            OLED_ShowString(0, 18 + i * 14, disp_buf, OLED_6X8);
        }
    }

    if (esp_tx_log_count > 3)
    {
        if (tx_log_scroll > 0)
            OLED_ShowChar(120, 18, '^', OLED_6X8);
        if (tx_log_scroll + 3 < esp_tx_log_count)
            OLED_ShowChar(120, 50, 'v', OLED_6X8);
    }

    OLED_Update();
}

/* =====================================================================
 *  Toggle 回调 — 蜂鸣器 / 继电器
 * ===================================================================== */

static void OnBuzzerToggle(void)
{
    if (buzzer_state)
    {
        Buzzer_On();
        /* 手动开启时取消自动报警 */
        buzzer_alarm_active = 0;
    }
    else
    {
        Buzzer_Off();
        buzzer_alarm_active = 0;
    }
}

static void OnRelayToggle(void)
{
    if (relay_state)
    {
        Relay_On();
        if (esp_connected)
        {
            ESP_SendAndLog(esp_link_id, "LOCK:OPEN,MANUAL");
        }
    }
    else
    {
        Relay_Off();
        if (esp_connected)
        {
            ESP_SendAndLog(esp_link_id, "LOCK:CLOSE,MANUAL");
        }
    }
}

/* =====================================================================
 *  菜单初始化 — 构建菜单树 + 注册回调 + 启动
 * ===================================================================== */

void Menu_Init(void)
{
    /* ---- 创建所有菜单页 ---- */

    MF_Menu_t *menuRoot      = MF_CreateMenu("Main Menu");
    MF_Menu_t *menuFace      = MF_CreateMenu("Face Manage");
    MF_Menu_t *menuDelete    = MF_CreateMenu("Delete Face");
    MF_Menu_t *menuESP       = MF_CreateMenu("ESP WiFi");
    MF_Menu_t *menuActuator  = MF_CreateMenu("Actuator Ctrl");

    /* ---- 根菜单 ---- */
    MF_AddSubmenu(menuRoot, "Face Manage",    menuFace);
    MF_AddSubmenu(menuRoot, "ESP WiFi",       menuESP);
    MF_AddSubmenu(menuRoot, "Actuator Ctrl",  menuActuator);

    /* ---- 人脸管理子菜单 ---- */
    MF_AddCustomPage(menuFace, "Face Verify",  Page_FaceVerify);
    MF_AddCustomPage(menuFace, "Face Enroll",  Page_FaceEnroll);
    MF_AddSubmenu(menuFace,    "Delete Face",  menuDelete);
    MF_AddCustomPage(menuFace, "Face DB Info", Page_FaceInfo);

    /* ---- 删除人脸子菜单 ---- */
    MF_AddCustomPage(menuDelete, "Delete One", Page_DeleteOne);
    MF_AddCustomPage(menuDelete, "Delete All", Page_DeleteAll);

    /* ---- ESP WiFi 子菜单 ---- */
    MF_AddCustomPage(menuESP, "ESP01S Status", Page_ESPStatus);
    MF_AddCustomPage(menuESP, "RX CMD Log",    Page_ESPRxLog);
    MF_AddCustomPage(menuESP, "TX Data Log",   Page_ESPTxLog);

    /* ---- 执行器控制子菜单 ---- */
    MF_AddToggle(menuActuator, "Buzzer",  &buzzer_state, OnBuzzerToggle);
    MF_AddToggle(menuActuator, "Relay",   &relay_state,  OnRelayToggle);

    /* ---- 注册 FM225 回调 ---- */
    FM225_SetMatchedCallback(OnFaceMatched);
    FM225_SetUnmatchedCallback(OnFaceUnmatched);
    FM225_SetInvalidCallback(OnFaceInvalid);
    FM225_SetEnrollDoneCallback(OnEnrollDone);
    FM225_SetEnrollFailCallback(OnEnrollFail);
    FM225_SetFaceCountCallback(OnFaceCount);

    /* ---- 光标样式 & 启动 ---- */
    MF_SetCursorStyle(MF_CURSOR_REVERSE);
    MF_Start(menuRoot);
}

/* =====================================================================
 *  ESP 初始化状态设置
 * ===================================================================== */

void Menu_SetESPInitOK(uint8_t ok)
{
    esp_init_ok = ok;
}

/* =====================================================================
 *  周期性处理 — 在主循环中调用
 * ===================================================================== */

void Menu_Tick(void)
{
    /* 蜂鸣器报警自动关闭 */
    Buzzer_AlarmTick();
}
