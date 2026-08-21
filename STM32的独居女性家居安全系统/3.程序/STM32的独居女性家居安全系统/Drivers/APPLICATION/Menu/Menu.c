#include ".\Application\Menu\Menu.h"
#include <stdio.h>

/* ==================== 全局变量定义 ==================== */

uint8_t  monitor_mode    = 0;    /* 0=OFF, 1=日常, 2=离家, 3=哨兵 */
int32_t  temp_threshold  = 40;   /* 温度阈值, 默认40°C */
int32_t  humi_threshold  = 80;   /* 湿度阈值, 默认80%  */
uint8_t  alarm_enabled   = 0;    /* 蜂鸣器手动开关 */
uint8_t  strobe_enabled  = 0;    /* 频闪灯手动开关 */
uint8_t  alarm_triggered = 0;    /* 报警触发标志    */
uint8_t  sentry_latch   = 0;    /* 哨兵锁定模式: 0=自动解除, 1=锁定 */

/* 内部: 哨兵锁定触发标志 (锁定模式下一旦触发则保持) */
static uint8_t sentry_latched = 0;

/* 内部使用：JQ8400 播放防重入标志 */
static uint8_t jq_playing = 0;

/* 内部使用：频闪灯手动开启标志 (区分手动/自动控制) */
static uint8_t strobe_manual = 0;

/* 文本行缓冲 */
static char menu_line_buf[32];

/* ==================== 模式名称表 ==================== */
static const char *mode_names[] = {
    "OFF",
    "DailyMon",
    "AwayGrd",
    "Sentry"
};

/* ==================== 安全监控逻辑 ==================== */

/**
 * @brief  在每次主循环中调用，根据当前模式执行安全检测
 */
static void Security_Process(void)
{
    uint8_t temp = DHT11_GetTemperature();
    uint8_t humi = DHT11_GetHumidity();
    uint8_t pir  = SR505_Read();
    uint8_t door = DoorWinMagSensor_Read(); /* 1=闭合, 0=断开(有人闯入) */

    uint8_t need_av_alarm   = 0;  /* 声光报警 */
    uint8_t need_voice      = 0;  /* 语音播报 */
    uint8_t need_strobe     = 0;  /* 强光频闪 */

    /* ---- 温湿度异常检测 (所有模式下生效) ---- */
    if ((int32_t)temp >= temp_threshold || (int32_t)humi >= humi_threshold)
    {
        need_av_alarm = 1;
    }

    /* ---- 模式相关检测 ---- */
    switch (monitor_mode)
    {
    case 1: /* 日常监控: SR505 检测到人 → 声光报警 */
        if (pir)
            need_av_alarm = 1;
        break;

    case 2: /* 离家布防: 门磁断开 → 语音播报 */
        if (door == 0)
            need_voice = 1;
        break;

    case 3: /* 主动哨兵: PIR检测到人 且 门磁断开 → 语音+频闪 */
        if (pir && door == 0)
        {
            need_voice  = 1;
            need_strobe = 1;
        }
        break;

    default: /* OFF */
        break;
    }

    /* ---- 执行报警动作 ---- */

    /* 声光报警 (蜂鸣器 + LED) */
    if (need_av_alarm)
    {
        AV_Alarm_On();
        alarm_triggered = 1;
    }
    else if (!alarm_enabled)
    {
        /* 仅当手动开关未打开时才自动关闭 */
        AV_Alarm_Off();
        alarm_triggered = 0;
    }

    /* ---- 哨兵锁定逻辑 ---- */
    if (monitor_mode == 3 && sentry_latch)
    {
        /* 锁定模式：一旦触发就保持 */
        if (need_voice || need_strobe)
            sentry_latched = 1;

        if (sentry_latched)
        {
            need_voice  = 1;
            need_strobe = 1;
        }
    }

    /* 语音播报 */
    if (need_voice)
    {
        if (!jq_playing)
        {
            JQ8400_SelectDevice(DEVICE_FLASH);
            JQ8400_SetPlayMode(PLAYMODE_SINGLE_LOOP);
            JQ8400_PlayTrack(1);
            jq_playing = 1;
        }
    }
    else
    {
        if (jq_playing)
        {
            JQ8400_Stop();
            jq_playing = 0;
        }
    }

    /* 强光频闪 */
    if (need_strobe)
    {
        if (!strobe_enabled)
        {
            Strobe_Blink_Start(200);
            strobe_enabled = 1;
        }
    }
    else if (!strobe_manual)
    {
        if (strobe_enabled)
        {
            Strobe_Blink_Stop();
            strobe_enabled = 0;
        }
    }
}

/* ==================== 模式切换回调 ==================== */

static void Mode_SetOFF(void)
{
    monitor_mode = 0;
    /* 切换到OFF时，停止所有自动报警 */
    alarm_triggered = 0;
    sentry_latched = 0;
    if (jq_playing) { JQ8400_Stop(); jq_playing = 0; }
    if (strobe_enabled) { Strobe_Blink_Stop(); strobe_enabled = 0; strobe_manual = 0; }
}

static void Mode_SetDaily(void)
{
    monitor_mode = 1;
    sentry_latched = 0;
    if (jq_playing) { JQ8400_Stop(); jq_playing = 0; }
    if (strobe_enabled) { Strobe_Blink_Stop(); strobe_enabled = 0; strobe_manual = 0; }
}

static void Mode_SetAway(void)
{
    monitor_mode = 2;
    sentry_latched = 0;
}

static void Mode_SetSentry(void)
{
    monitor_mode = 3;
    sentry_latched = 0;  /* 进入哨兵模式时清除锁定状态 */
}

/** 手动复位哨兵报警（锁定模式下使用） */
static void Sentry_ResetAlarm(void)
{
    sentry_latched = 0;
    if (jq_playing) { JQ8400_Stop(); jq_playing = 0; }
    if (strobe_enabled) { Strobe_Blink_Stop(); strobe_enabled = 0; strobe_manual = 0; }
}

/* ==================== 光标样式回调 ==================== */

static void Cursor_SetReverse(void)
{
    MF_SetCursorStyle(MF_CURSOR_REVERSE);
}

static void Cursor_SetBox(void)
{
    MF_SetCursorStyle(MF_CURSOR_BOX);
}

/* ==================== Toggle 回调 ==================== */

static void OnAlarmToggle(void)
{
    if (alarm_enabled)
        AV_Alarm_On();
    else
        AV_Alarm_Off();
}

static void OnStrobeToggle(void)
{
    if (strobe_enabled)
    {
        Strobe_Blink_Start(200);
        strobe_manual = 1;
    }
    else
    {
        Strobe_Blink_Stop();
        strobe_manual = 0;
    }
}

/* ==================== 自定义页面: 温湿度显示 ==================== */

static void TempHumiPage(KeyEvent_t key, uint8_t *exit_flag)
{
    if (key == KEY_BACK)
    {
        *exit_flag = 1;
        return;
    }

    uint8_t temp = DHT11_GetTemperature();
    uint8_t humi = DHT11_GetHumidity();

    /* 标题 */
    OLED_ShowString(0, 0, "Temp & Humi", MF_FONT);
    OLED_DrawLine(0, MF_TITLE_HEIGHT, MF_SCREEN_WIDTH, MF_TITLE_HEIGHT);

    /* 第1行: 温度 + 温度阈值 */
    snprintf(menu_line_buf, sizeof(menu_line_buf), "T:%2dC TH:%ldC",
             temp, (long)temp_threshold);
    OLED_ShowString(0, 16, menu_line_buf, MF_FONT);

    /* 第2行: 湿度 + 湿度阈值 */
    snprintf(menu_line_buf, sizeof(menu_line_buf), "H:%2d%% HH:%ld%%",
             humi, (long)humi_threshold);
    OLED_ShowString(0, 32, menu_line_buf, MF_FONT);

    /* 第3行底部: 当前模式 */
    snprintf(menu_line_buf, sizeof(menu_line_buf), "Mode:%s",
             mode_names[monitor_mode]);
    OLED_ShowString(0, 48, menu_line_buf, OLED_6X8);

    /* 右下角提示 */
    OLED_ShowString(72, 48, "B:Back", OLED_6X8);
}

/* ==================== 自定义页面: 传感器状态 ==================== */

static void SensorPage(KeyEvent_t key, uint8_t *exit_flag)
{
    if (key == KEY_BACK)
    {
        *exit_flag = 1;
        return;
    }

    uint8_t pir  = SR505_Read();
    uint8_t door = DoorWinMagSensor_Read();

    /* 标题 */
    OLED_ShowString(0, 0, "Sensor State", MF_FONT);
    OLED_DrawLine(0, MF_TITLE_HEIGHT, MF_SCREEN_WIDTH, MF_TITLE_HEIGHT);

    /* PIR */
    if (pir)
        OLED_ShowString(0, 16, "PIR:  Detect!", MF_FONT);
    else
        OLED_ShowString(0, 16, "PIR:  Idle   ", MF_FONT);

    /* 门磁 */
    if (door)
        OLED_ShowString(0, 32, "Door: Closed ", MF_FONT);
    else
        OLED_ShowString(0, 32, "Door: OPEN!! ", MF_FONT);

    /* 底部: 当前模式 */
    snprintf(menu_line_buf, sizeof(menu_line_buf), "Mode:%s",
             mode_names[monitor_mode]);
    OLED_ShowString(0, 48, menu_line_buf, OLED_6X8);

    OLED_ShowString(72, 48, "B:Back", OLED_6X8);
}

/* ==================== 菜单构建 ==================== */

void Menu_Init(void)
{
    /* -------- 创建所有菜单页 -------- */
    MF_Menu_t *mainMenu      = MF_CreateMenu("Home Security");
    MF_Menu_t *thresholdMenu = MF_CreateMenu("Set Threshold");
    MF_Menu_t *modeMenu      = MF_CreateMenu("Monitor Mode");
    MF_Menu_t *cursorMenu    = MF_CreateMenu("Cursor Style");
    MF_Menu_t *alarmCtrlMenu = MF_CreateMenu("Alarm & LED");

    /* -------- 阈值设置子菜单 -------- */
    MF_AddValue(thresholdMenu, "TempTH",
                &temp_threshold, 0, 60, 1, "C", NULL);
    MF_AddValue(thresholdMenu, "HumiTH",
                &humi_threshold, 0, 99, 1, "%", NULL);

    /* -------- 模式选择子菜单 -------- */
    MF_AddAction(modeMenu, "OFF",        Mode_SetOFF);
    MF_AddAction(modeMenu, "Daily Mon",  Mode_SetDaily);
    MF_AddAction(modeMenu, "Away Guard", Mode_SetAway);
    MF_AddAction(modeMenu, "Sentry",     Mode_SetSentry);
    MF_AddToggle(modeMenu, "Latch",      &sentry_latch, NULL);
    MF_AddAction(modeMenu, "ResetAlarm", Sentry_ResetAlarm);

    /* -------- 光标样式子菜单 -------- */
    MF_AddAction(cursorMenu, "Reverse", Cursor_SetReverse);
    MF_AddAction(cursorMenu, "Box",     Cursor_SetBox);

    /* -------- 报警/频闪控制子菜单 -------- */
    MF_AddToggle(alarmCtrlMenu, "Alarm",
                 &alarm_enabled, OnAlarmToggle);
    MF_AddToggle(alarmCtrlMenu, "Strobe",
                 &strobe_enabled, OnStrobeToggle);

    /* -------- 主菜单 -------- */
    MF_AddCustomPage(mainMenu, "Temp/Humi",  TempHumiPage);
    MF_AddSubmenu(mainMenu,    "Threshold",  thresholdMenu);
    MF_AddSubmenu(mainMenu,    "Mode",       modeMenu);
    MF_AddSubmenu(mainMenu,    "Cursor",     cursorMenu);
    MF_AddCustomPage(mainMenu, "Sensors",    SensorPage);
    MF_AddSubmenu(mainMenu,    "Alarm/LED",  alarmCtrlMenu);

    /* -------- 启动菜单系统 -------- */
    MF_Start(mainMenu);
}

/* ==================== 菜单主循环 ==================== */

void Menu_Loop(void)
{
    /* 先执行安全监控逻辑 */
    Security_Process();

    /* 再执行菜单框架循环 (按键扫描 + 渲染 + OLED刷新) */
    MF_Loop();
}
