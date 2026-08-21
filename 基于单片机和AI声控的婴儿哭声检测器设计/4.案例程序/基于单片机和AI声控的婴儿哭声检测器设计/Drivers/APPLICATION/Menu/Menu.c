#include ".\Application\Menu\Menu.h"
#include <stdio.h>
#include <string.h>

/* ---- 硬件驱动头文件 ---- */
#include ".\Hardware\OLED\OLED.h"
#include ".\Hardware\KEY\KEY.h"
#include ".\Hardware\Buzzer\Buzzer.h"
#include ".\Hardware\LED\LED.h"
#include ".\Hardware\Vibrator\Vibrator.h"
#include ".\Hardware\SoundSensor\SoundSensor.h"
#include ".\Hardware\CrySensor\CrySensor.h"
#include ".\Hardware\Bluetooth\Bluetooth.h"

/* ==================================================================
 *  配置参数
 * ================================================================== */

#define SOUND_THRESHOLD_DEFAULT  2000u   /* 声音 ADC 触发阈值（0~4095）       */
#define SOUND_THRESHOLD_MIN      500u   /* 阈值最小值                         */
#define SOUND_THRESHOLD_MAX     3500u   /* 阈值最大值                         */
#define SOUND_THRESHOLD_STEP     200u   /* 按键调节步长                       */

#define DETECT_TIMEOUT_MS       3000u   /* AI 识别超时时间 (ms)               */
#define ALARM_DURATION_MS      30000u   /* 报警持续时间 (ms)                  */
#define ALARM_TOGGLE_MS          500u   /* 报警闪烁 / 间歇周期 (ms)           */

#define OLED_REFRESH_MS          200u   /* OLED 刷新周期 (ms)                 */

#define SOUND_SAMPLE_COUNT         4u   /* 声音采样平均次数                   */
#define SOUND_SAMPLE_INTERVAL      0u   /* 采样间隔 (ms)                      */

/* ==================================================================
 *  私有变量
 * ================================================================== */

static SysState_t  s_sysState   = SYS_STATE_IDLE;
static AlarmMode_t s_alarmMode  = ALARM_MODE_SOUND;

static uint16_t s_soundThreshold = SOUND_THRESHOLD_DEFAULT;
static uint16_t s_lastSoundValue = 0u;

/* 时间戳 */
static uint32_t s_detectStartTick  = 0u;   /* 进入 DETECTING 的时刻   */
static uint32_t s_alarmStartTick   = 0u;   /* 进入 ALARMING 的时刻    */
static uint32_t s_alarmToggleTick  = 0u;   /* 报警闪烁翻转时刻        */
static uint32_t s_oledRefreshTick  = 0u;   /* OLED 上次刷新时刻       */

static uint8_t  s_alarmToggleFlag  = 0u;   /* 报警输出当前是 ON / OFF */

static uint32_t s_cryTimestamp     = 0u;   /* 最近一次哭声检测时间戳  */
static uint8_t  s_cryDetected      = 0u;   /* 是否曾检测到过哭声      */

/* ==================================================================
 *  私有函数原型
 * ================================================================== */
static void Menu_HandleKeys(void);
static void Menu_StateIdle(void);
static void Menu_StateDetecting(void);
static void Menu_StateAlarming(void);
static void Menu_StartAlarm(void);
static void Menu_StopAlarm(void);
static void Menu_AlarmToggle(void);
static void Menu_RefreshOLED(void);
static void Menu_BluetoothPushCryEvent(void);
static void Menu_FormatUptime(uint32_t ms, char *buf, uint8_t bufSize);

/* ==================================================================
 *  公共函数实现
 * ================================================================== */

/**
 * @brief  业务逻辑初始化
 */
void Menu_Init(void)
{
    s_sysState  = SYS_STATE_IDLE;
    s_alarmMode = ALARM_MODE_SOUND;

    /* 确保所有输出关闭 */
    Buzzer_Off();
    LED_Off();
    Vibrator_Off();

    /* 重置 CrySensor 边沿状态，避免首次误判 */
    CrySensor_ResetEdgeState();

    /* OLED 首屏 */
    OLED_Clear();
    OLED_ShowString(0,  0, "Baby Cry Detect", OLED_8X16);
    OLED_ShowString(0, 16, "Mode: Sound", OLED_8X16);
    OLED_ShowString(0, 32, "Sound: ----", OLED_8X16);
    OLED_ShowString(0, 48, "Status: Idle", OLED_8X16);
    OLED_Update();

    /* 蓝牙发送上线通知 */
    Bluetooth_SendString("[SYS]Baby Cry Detector Online\r\n");

    s_oledRefreshTick = HAL_GetTick();
}

/**
 * @brief  业务主任务（主循环周期调用）
 */
void Menu_Task(void)
{
    /* 1. 按键处理 —— 任何状态均可响应 */
    Menu_HandleKeys();

    /* 2. 状态机运转 */
    switch (s_sysState)
    {
    case SYS_STATE_IDLE:
        Menu_StateIdle();
        break;

    case SYS_STATE_DETECTING:
        Menu_StateDetecting();
        break;

    case SYS_STATE_ALARMING:
        Menu_StateAlarming();
        break;

    default:
        s_sysState = SYS_STATE_IDLE;
        break;
    }

    /* 3. OLED 定时刷新 */
    if ((HAL_GetTick() - s_oledRefreshTick) >= OLED_REFRESH_MS)
    {
        s_oledRefreshTick = HAL_GetTick();
        Menu_RefreshOLED();
    }
}

/* ==================================================================
 *  按键处理
 * ================================================================== */

static void Menu_HandleKeys(void)
{
    KeyEvent_t key = Key_Scan();

    switch (key)
    {
    case KEY_ENTER:
        /* 切换报警模式 */
        if (s_alarmMode == ALARM_MODE_SOUND)
        {
            s_alarmMode = ALARM_MODE_MUTE;
            Bluetooth_SendString("[MODE]Mute\r\n");
        }
        else
        {
            s_alarmMode = ALARM_MODE_SOUND;
            Bluetooth_SendString("[MODE]Sound\r\n");
        }
        /* 如果正在报警中，立即按新模式切换输出 */
        if (s_sysState == SYS_STATE_ALARMING)
        {
            Menu_StopAlarm();   /* 先关闭当前输出 */
            Menu_StartAlarm();  /* 按新模式重新启动 */
        }
        break;

    case KEY_BACK:
        /* 一键消警 */
        if (s_sysState == SYS_STATE_ALARMING)
        {
            Menu_StopAlarm();
            s_sysState = SYS_STATE_IDLE;
            CrySensor_ResetEdgeState();
            Bluetooth_SendString("[ACK]Alarm Cleared\r\n");
        }
        else if (s_sysState == SYS_STATE_DETECTING)
        {
            /* 取消检测，回待机 */
            s_sysState = SYS_STATE_IDLE;
            CrySensor_ResetEdgeState();
        }
        break;

    case KEY_UP:
        /* 增大灵敏度（降低阈值） */
        if (s_soundThreshold > SOUND_THRESHOLD_MIN + SOUND_THRESHOLD_STEP)
        {
            s_soundThreshold -= SOUND_THRESHOLD_STEP;
        }
        else
        {
            s_soundThreshold = SOUND_THRESHOLD_MIN;
        }
        break;

    case KEY_DOWN:
        /* 降低灵敏度（增大阈值） */
        if (s_soundThreshold < SOUND_THRESHOLD_MAX - SOUND_THRESHOLD_STEP)
        {
            s_soundThreshold += SOUND_THRESHOLD_STEP;
        }
        else
        {
            s_soundThreshold = SOUND_THRESHOLD_MAX;
        }
        break;

    default:
        break;
    }
}

/* ==================================================================
 *  状态机：IDLE —— 待机监听
 * ================================================================== */

static void Menu_StateIdle(void)
{
    /* 读取声音传感器模拟值 */
    s_lastSoundValue = SoundSensor_ReadAnalogAverage(SOUND_SAMPLE_COUNT,
                                                      SOUND_SAMPLE_INTERVAL);

    /* 也检查数字触发引脚 */
    uint8_t doTriggered = SoundSensor_IsTriggered();

    /* 判断是否超过阈值 */
    if ((s_lastSoundValue >= s_soundThreshold) || doTriggered)
    {
        /* 声音强度超标，启动 AI 识别 */
        s_sysState = SYS_STATE_DETECTING;
        s_detectStartTick = HAL_GetTick();
        CrySensor_ResetEdgeState();   /* 重置边沿，从当前开始检测 */
    }
}

/* ==================================================================
 *  状态机：DETECTING —— AI 识别中
 * ================================================================== */

static void Menu_StateDetecting(void)
{
    /* 持续读取声音值用于显示 */
    s_lastSoundValue = SoundSensor_ReadAnalog();

    /* 轮询 AI 模块输出的上升沿事件 */
    uint8_t events = CrySensor_GetRisingEvents();

    if (events & CRY_SENSOR_EVENT_P1_RISING)
    {
        /* P1 上升沿 → 婴儿哭声确认 */
        s_cryTimestamp = HAL_GetTick();
        s_cryDetected = 1u;

        /* 进入报警状态 */
        s_sysState = SYS_STATE_ALARMING;
        Menu_StartAlarm();

        /* 蓝牙推送哭声事件 */
        Menu_BluetoothPushCryEvent();
        return;
    }

    if (events & CRY_SENSOR_EVENT_P2_RISING)
    {
        /* P2 上升沿 → 非哭声 / 其他声音，回待机 */
        s_sysState = SYS_STATE_IDLE;
        CrySensor_ResetEdgeState();
        return;
    }

    /* 超时判断 */
    if ((HAL_GetTick() - s_detectStartTick) >= DETECT_TIMEOUT_MS)
    {
        /* AI 模块无响应，超时回待机 */
        s_sysState = SYS_STATE_IDLE;
        CrySensor_ResetEdgeState();
    }
}

/* ==================================================================
 *  状态机：ALARMING —— 报警中
 * ================================================================== */

static void Menu_StateAlarming(void)
{
    /* 持续读取声音值用于显示 */
    s_lastSoundValue = SoundSensor_ReadAnalog();

    /* 报警间歇翻转 */
    if ((HAL_GetTick() - s_alarmToggleTick) >= ALARM_TOGGLE_MS)
    {
        s_alarmToggleTick = HAL_GetTick();
        Menu_AlarmToggle();
    }

    /* 同时继续监听 AI 模块 —— 若再次触发可延长报警 */
    uint8_t events = CrySensor_GetRisingEvents();
    if (events & CRY_SENSOR_EVENT_P1_RISING)
    {
        /* 再次检测到哭声，延长报警 */
        s_alarmStartTick = HAL_GetTick();
        s_cryTimestamp = HAL_GetTick();

        /* 再次蓝牙推送 */
        Menu_BluetoothPushCryEvent();
    }

    /* 报警超时自动停止 */
    if ((HAL_GetTick() - s_alarmStartTick) >= ALARM_DURATION_MS)
    {
        Menu_StopAlarm();
        s_sysState = SYS_STATE_IDLE;
        CrySensor_ResetEdgeState();
        Bluetooth_SendString("[ACK]Alarm Auto-Stop\r\n");
    }
}

/* ==================================================================
 *  报警控制函数
 * ================================================================== */

/**
 * @brief  启动报警输出
 */
static void Menu_StartAlarm(void)
{
    s_alarmStartTick  = HAL_GetTick();
    s_alarmToggleTick = HAL_GetTick();
    s_alarmToggleFlag = 1u;

    if (s_alarmMode == ALARM_MODE_SOUND)
    {
        Buzzer_On();
        LED_On();
        Vibrator_Off();
    }
    else /* ALARM_MODE_MUTE */
    {
        Buzzer_Off();
        LED_Off();
        Vibrator_On();
    }
}

/**
 * @brief  停止所有报警输出
 */
static void Menu_StopAlarm(void)
{
    Buzzer_Off();
    LED_Off();
    Vibrator_Off();
    s_alarmToggleFlag = 0u;
}

/**
 * @brief  报警间歇翻转（500ms ON / 500ms OFF）
 */
static void Menu_AlarmToggle(void)
{
    s_alarmToggleFlag = !s_alarmToggleFlag;

    if (s_alarmMode == ALARM_MODE_SOUND)
    {
        if (s_alarmToggleFlag)
        {
            Buzzer_On();
            LED_On();
        }
        else
        {
            Buzzer_Off();
            LED_Off();
        }
        Vibrator_Off();  /* 确保振动关闭 */
    }
    else /* ALARM_MODE_MUTE */
    {
        if (s_alarmToggleFlag)
        {
            Vibrator_On();
        }
        else
        {
            Vibrator_Off();
        }
        Buzzer_Off();    /* 确保蜂鸣器关闭 */
        LED_Off();       /* 确保 LED 关闭   */
    }
}

/* ==================================================================
 *  OLED 显示刷新
 * ================================================================== */

static void Menu_RefreshOLED(void)
{
    char line[22];  /* 128/6 ≈ 21 字符 (6x8 字体) 或 128/8 = 16 字符 (8x16 字体) */

    OLED_Clear();

    /* ---- 第 0 行：标题 ---- */
    if (s_sysState == SYS_STATE_ALARMING)
    {
        OLED_ShowString(0, 0, "!! BABY CRYING !!", OLED_8X16);
    }
    else
    {
        OLED_ShowString(0, 0, "BabyCry Detector", OLED_8X16);
    }

    /* ---- 第 2 行：报警模式 ---- */
    if (s_alarmMode == ALARM_MODE_SOUND)
    {
        OLED_ShowString(0, 16, "Mode: Sound", OLED_8X16);
    }
    else
    {
        OLED_ShowString(0, 16, "Mode: Mute", OLED_8X16);
    }

    /* ---- 第 4 行：声音值 + 阈值 ---- */
    snprintf(line, sizeof(line), "S:%4u  T:%4u",
             (unsigned int)s_lastSoundValue,
             (unsigned int)s_soundThreshold);
    OLED_ShowString(0, 32, line, OLED_8X16);

    /* ---- 第 6 行：状态信息 ---- */
    switch (s_sysState)
    {
    case SYS_STATE_IDLE:
        OLED_ShowString(0, 48, "Status: Idle", OLED_8X16);
        break;

    case SYS_STATE_DETECTING:
        OLED_ShowString(0, 48, "Detecting...", OLED_8X16);
        break;

    case SYS_STATE_ALARMING:
    {
        char timeBuf[12];
        Menu_FormatUptime(s_cryTimestamp, timeBuf, sizeof(timeBuf));
        snprintf(line, sizeof(line), "Cry@%s", timeBuf);
        OLED_ShowString(0, 48, line, OLED_8X16);
        break;
    }

    default:
        break;
    }

    OLED_Update();
}

/* ==================================================================
 *  蓝牙推送
 * ================================================================== */

/**
 * @brief  通过蓝牙发送哭声事件（含时间戳）
 */
static void Menu_BluetoothPushCryEvent(void)
{
    char msg[64];
    char timeBuf[12];

    Menu_FormatUptime(s_cryTimestamp, timeBuf, sizeof(timeBuf));
    snprintf(msg, sizeof(msg), "[CRY]TIME:%s\r\n", timeBuf);
    Bluetooth_SendString(msg);
}

/* ==================================================================
 *  工具函数
 * ================================================================== */

/**
 * @brief  将 ms 时间戳转为 HH:MM:SS 字符串
 */
static void Menu_FormatUptime(uint32_t ms, char *buf, uint8_t bufSize)
{
    uint32_t totalSec = ms / 1000u;
    uint8_t  h = (uint8_t)(totalSec / 3600u);
    uint8_t  m = (uint8_t)((totalSec % 3600u) / 60u);
    uint8_t  s = (uint8_t)(totalSec % 60u);

    snprintf(buf, bufSize, "%02u:%02u:%02u",
             (unsigned int)h, (unsigned int)m, (unsigned int)s);
}
