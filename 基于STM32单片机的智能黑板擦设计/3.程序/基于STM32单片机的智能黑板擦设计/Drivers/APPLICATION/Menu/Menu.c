/**
 * @file   Menu.c
 * @brief  智能黑板擦 — 菜单树构建、自定义页面渲染、自动控制逻辑、蓝牙命令处理
 * @note   基于 MenuFramework 实现，所有应用全局变量定义在此文件
 *
 * 【优化要点】
 *  - 消除所有 snprintf 调用，改用 OLED_Printf / OLED_ShowNum 等减少栈消耗
 *  - 所有栈变量控制在最小范围
 *  - 蓝牙上报使用静态缓冲区
 *  - 执行器同步增加脏标记，避免无谓重复写入
 */

#include "APPLICATION/Menu/Menu.h"
#include "APPLICATION/Menu/MenuFramework.h"
#include "Hardware/OLED/OLED.h"
#include "Hardware/KEY/Key.h"
#include "Hardware/TB6612/TB6612.h"
#include "Hardware/Bluetooth/Bluetooth.h"
#include "Hardware/DC01/DC01.h"
#include "Hardware/Water Level/Water_Level.h"
#include "Hardware/FAN/FAN.h"
#include "Hardware/Mist Module/Mist_Module.h"
#include <stdio.h>
#include <string.h>

/* ==================== 全局变量定义 ==================== */

/* 传感器数据 */
uint16_t g_pm25           = 0;
uint16_t g_pm10           = 0;
uint8_t  g_water_percent  = 0;

/* 阈值设置 */
int32_t  g_dust_threshold  = 100;   /* 默认灰尘阈值 100 μg/m³ */
int32_t  g_water_threshold = 20;    /* 默认水位阈值 20%        */

/* 毛刷/电机转速 */
int32_t  g_brush_speed     = 60;    /* 默认转速 60%            */

/* 功能开关 */
uint8_t  g_fan_on          = 0;
uint8_t  g_mist_on         = 0;
uint8_t  g_brush_on        = 0;

/* 自动清洁状态 */
uint8_t  g_auto_cleaning   = 0;

/* 手动覆盖标记 — 用户手动关闭后，自动模式不再覆盖 */
static uint8_t s_manual_fan_off   = 0;
static uint8_t s_manual_mist_off  = 0;
static uint8_t s_manual_brush_off = 0;

/* 闪烁计数器 (用于水位不足警告闪烁) */
static uint8_t s_blink_cnt = 0;

/* 执行器脏标记 — 仅状态变更时才写硬件 */
static uint8_t s_actuator_dirty = 1;

/* 蓝牙上报 — 使用静态缓冲区，避免栈分配 */
static char s_bt_tx_buf[80];

/* ==================== 菜单页指针 ==================== */
static MF_Menu_t *menu_main    = NULL;
static MF_Menu_t *menu_switch  = NULL;

/* ==================== 辅助工具：整数转字符串 (无 snprintf) ==================== */

/**
 * @brief  将无符号整数转为十进制字符串
 * @param  buf  输出缓冲区 (至少 6 字节)
 * @param  val  0~65535
 * @retval 字符串长度
 */
static uint8_t u16_to_str(char *buf, uint16_t val)
{
    char tmp[6];
    uint8_t i = 0, len;

    if (val == 0)
    {
        buf[0] = '0';
        buf[1] = '\0';
        return 1;
    }

    while (val > 0)
    {
        tmp[i++] = '0' + (char)(val % 10);
        val /= 10;
    }
    len = i;

    /* 反转 */
    while (i > 0)
    {
        i--;
        *buf++ = tmp[i];
    }
    *buf = '\0';
    return len;
}

/**
 * @brief  将有符号32位整数转为十进制字符串
 * @param  buf  输出缓冲区 (至少 12 字节)
 * @param  val  值
 * @retval 字符串长度
 */
static uint8_t i32_to_str(char *buf, int32_t val)
{
    uint8_t off = 0;
    uint32_t uv;

    if (val < 0)
    {
        buf[0] = '-';
        off = 1;
        uv = (uint32_t)(-(val + 1)) + 1u;
    }
    else
    {
        uv = (uint32_t)val;
    }

    return off + u16_to_str(buf + off, (uint16_t)uv);
}

/* ==================== 回调函数声明 ==================== */

/* 自定义页面回调 */
static void Page_RealtimeData(KeyEvent_t key, uint8_t *exit_flag);
static void Page_BluetoothStatus(KeyEvent_t key, uint8_t *exit_flag);

/* Toggle 变更回调 */
static void OnFanToggle(void);
static void OnMistToggle(void);
static void OnBrushToggle(void);

/* Value 变更回调 */
static void OnBrushSpeedChange(int32_t new_val);
static void OnDustThresholdChange(int32_t new_val);

/* Action 回调 */
static void OnCursorStyleToggle(void);

/* ==================== 回调函数实现 ==================== */

/**
 * @brief  实时数据自定义页面
 *         用 6X8 小字体密集显示所有传感器数据
 *         不使用 snprintf，改用手动拼接 + ShowString/ShowNum
 */
static void Page_RealtimeData(KeyEvent_t key, uint8_t *exit_flag)
{
    if (key == KEY_BACK)
    {
        *exit_flag = 1;
        return;
    }

    OLED_Clear();

    /* 标题 (8X16) */
    OLED_ShowString(20, 0, "Real-Time", OLED_8X16);

    /* 分隔线 */
    OLED_DrawLine(0, 15, 127, 15);

    /* 数据区 (6X8) — 从 Y=17 开始, 每行 9px */
    /* PM2.5 行 */
    OLED_ShowString(0, 17, "PM2.5:", OLED_6X8);
    OLED_ShowNum(36, 17, g_pm25, 5, OLED_6X8);
    OLED_ShowString(66, 17, "ug/m3", OLED_6X8);

    /* PM10 行 */
    OLED_ShowString(0, 26, "PM10 :", OLED_6X8);
    OLED_ShowNum(36, 26, g_pm10, 5, OLED_6X8);
    OLED_ShowString(66, 26, "ug/m3", OLED_6X8);

    /* 水位行 */
    OLED_ShowString(0, 35, "Water:", OLED_6X8);
    OLED_ShowNum(36, 35, g_water_percent, 3, OLED_6X8);
    OLED_ShowChar(54, 35, '%', OLED_6X8);

    /* 毛刷行 */
    OLED_ShowString(0, 44, "Brush:", OLED_6X8);
    OLED_ShowNum(36, 44, (uint32_t)g_brush_speed, 3, OLED_6X8);
    OLED_ShowChar(54, 44, '%', OLED_6X8);
    OLED_ShowString(66, 44, g_brush_on ? "[ON]" : "[OFF]", OLED_6X8);

    /* 风扇和喷水状态 */
    OLED_ShowString(0, 53, "FAN:", OLED_6X8);
    OLED_ShowString(24, 53, g_fan_on ? "ON " : "OFF", OLED_6X8);
    OLED_ShowString(48, 53, "MIST:", OLED_6X8);
    OLED_ShowString(78, 53, g_mist_on ? "ON " : "OFF", OLED_6X8);

    /* 水位不足警告 — 闪烁显示 */
    if (g_water_percent < (uint8_t)g_water_threshold)
    {
        s_blink_cnt++;
        if (s_blink_cnt & 0x02)
        {
            OLED_ShowString(96, 53, "!LOW", OLED_6X8);
        }
    }

    OLED_Update();
}

/**
 * @brief  蓝牙状态自定义页面
 *         进入页面时自动测试一次（非阻塞方式：只在有按键时测试）
 */
static void Page_BluetoothStatus(KeyEvent_t key, uint8_t *exit_flag)
{
    static uint8_t bt_tested  = 0;
    static uint8_t bt_result  = 0;

    if (key == KEY_BACK)
    {
        bt_tested = 0;
        *exit_flag = 1;
        return;
    }

    /* 只在用户按 ENTER 时测试，避免自动阻塞 500ms */
    if (key == KEY_ENTER)
    {
        bt_result = Bluetooth_AT_Test();
        bt_tested = 1;
    }

    OLED_Clear();

    /* 标题 */
    OLED_ShowString(16, 0, "Bluetooth", OLED_8X16);
    OLED_DrawLine(0, 15, 127, 15);

    /* 连接状态 */
    if (!bt_tested)
    {
        OLED_ShowString(4, 24, "Press ENT", OLED_8X16);
        OLED_ShowString(4, 40, "to test", OLED_8X16);
    }
    else if (bt_result)
    {
        OLED_ShowString(16, 28, "Connected", OLED_8X16);
    }
    else
    {
        OLED_ShowString(0, 28, "Disconnect", OLED_8X16);
    }

    /* 提示 */
    OLED_ShowString(0, 56, "ENT:Test BACK:Ret", OLED_6X8);

    OLED_Update();
}

/**
 * @brief  风扇 Toggle 变更回调
 */
static void OnFanToggle(void)
{
    s_actuator_dirty = 1;
    if (!g_fan_on && g_auto_cleaning)
        s_manual_fan_off = 1;
    else
        s_manual_fan_off = 0;
}

/**
 * @brief  喷水 Toggle 变更回调
 */
static void OnMistToggle(void)
{
    s_actuator_dirty = 1;
    if (!g_mist_on && g_auto_cleaning)
        s_manual_mist_off = 1;
    else
        s_manual_mist_off = 0;
}

/**
 * @brief  毛刷 Toggle 变更回调
 */
static void OnBrushToggle(void)
{
    s_actuator_dirty = 1;
    if (!g_brush_on && g_auto_cleaning)
        s_manual_brush_off = 1;
    else
        s_manual_brush_off = 0;
}

/**
 * @brief  毛刷转速变更回调 — 如果毛刷正在运行，标记需要更新
 */
static void OnBrushSpeedChange(int32_t new_val)
{
    (void)new_val;
    if (g_brush_on)
        s_actuator_dirty = 1;
}

/**
 * @brief  灰尘阈值变更回调 (预留)
 */
static void OnDustThresholdChange(int32_t new_val)
{
    (void)new_val;
}

/**
 * @brief  光标样式切换 Action 回调
 */
static void OnCursorStyleToggle(void)
{
    if (MF_GetCursorStyle() == MF_CURSOR_REVERSE)
        MF_SetCursorStyle(MF_CURSOR_BOX);
    else
        MF_SetCursorStyle(MF_CURSOR_REVERSE);
}

/* ==================== 菜单初始化 ==================== */

void Menu_Init(void)
{
    /* ---- 创建菜单页 ---- */
    menu_main   = MF_CreateMenu("SmartEraser");
    menu_switch = MF_CreateMenu("Func Switch");

    /* ---- 主菜单条目 ---- */

    /* 1. 实时数据 (自定义整页) */
    MF_AddCustomPage(menu_main, "Real-Time", Page_RealtimeData);

    /* 2. 灰尘阈值 (数值调节) */
    MF_AddValue(menu_main, "DustThres",
                &g_dust_threshold, 10, 500, 10, "ug", OnDustThresholdChange);

    /* 3. 水位阈值 (数值调节) */
    MF_AddValue(menu_main, "WaterThres",
                &g_water_threshold, 5, 50, 5, "%", NULL);

    /* 4. 毛刷转速 (数值调节) */
    MF_AddValue(menu_main, "BrushSpd",
                &g_brush_speed, 0, 100, 10, "%", OnBrushSpeedChange);

    /* 5. 功能开关 (子菜单) */
    MF_AddSubmenu(menu_main, "FuncSwitch", menu_switch);

    /* 6. 蓝牙状态 (自定义整页) */
    MF_AddCustomPage(menu_main, "BT Status", Page_BluetoothStatus);

    /* 7. 光标样式 (动作按钮) */
    MF_AddAction(menu_main, "CursorStyl", OnCursorStyleToggle);

    /* ---- 功能开关子菜单条目 ---- */
    MF_AddToggle(menu_switch, "FAN",   &g_fan_on,   OnFanToggle);
    MF_AddToggle(menu_switch, "Mist",  &g_mist_on,  OnMistToggle);
    MF_AddToggle(menu_switch, "Brush", &g_brush_on,  OnBrushToggle);

    /* ---- 启动菜单系统 ---- */
    MF_Start(menu_main);
}

/* ==================== 自动控制逻辑 ==================== */

void Menu_AutoControl(void)
{
    uint8_t old_fan   = g_fan_on;
    uint8_t old_mist  = g_mist_on;
    uint8_t old_brush = g_brush_on;

    /* 灰尘浓度超过阈值 → 触发自动清洁 */
    if (g_pm25 >= (uint16_t)g_dust_threshold)
    {
        if (!g_auto_cleaning)
        {
            g_auto_cleaning    = 1;
            s_manual_fan_off   = 0;
            s_manual_mist_off  = 0;
            s_manual_brush_off = 0;
        }

        if (!s_manual_fan_off)
            g_fan_on = 1;

        if (!s_manual_brush_off)
            g_brush_on = 1;

        if (g_water_percent >= (uint8_t)g_water_threshold)
        {
            if (!s_manual_mist_off)
                g_mist_on = 1;
        }
        else
        {
            g_mist_on = 0;
        }
    }
    else
    {
        if (g_auto_cleaning)
        {
            g_auto_cleaning = 0;

            if (!s_manual_fan_off)
                g_fan_on = 0;
            if (!s_manual_mist_off)
                g_mist_on = 0;
            if (!s_manual_brush_off)
                g_brush_on = 0;

            s_manual_fan_off   = 0;
            s_manual_mist_off  = 0;
            s_manual_brush_off = 0;
        }
    }

    /* 检查状态是否有变化 */
    if (g_fan_on != old_fan || g_mist_on != old_mist || g_brush_on != old_brush)
        s_actuator_dirty = 1;
}

void Menu_ToggleBrush(void)
{
    g_brush_on ^= 1u;
    s_actuator_dirty = 1;

    if (!g_brush_on && g_auto_cleaning)
        s_manual_brush_off = 1;
    else
        s_manual_brush_off = 0;
}

void Menu_RequestActuatorSync(void)
{
    s_actuator_dirty = 1;
}

/* ==================== 执行器同步 (仅脏时写硬件) ==================== */

void Menu_ApplyActuators(void)
{
    if (!s_actuator_dirty)
        return;

    s_actuator_dirty = 0;

    /* 风扇 */
    if (g_fan_on)
        FAN_On();
    else
        FAN_Off();

    /* 喷水 */
    if (g_mist_on)
        Mist_Module_On();
    else
        Mist_Module_Off();

    /* 毛刷/电机 */
    if (g_brush_on)
    {
        TB6612_SetMotor(TB6612_MOTOR_A, TB6612_DIR_FORWARD, (uint8_t)g_brush_speed);
        TB6612_SetMotor(TB6612_MOTOR_B, TB6612_DIR_FORWARD, (uint8_t)g_brush_speed);
    }
    else
    {
        TB6612_StopAll();
    }
}

/* ==================== 蓝牙命令处理 ==================== */

void Menu_HandleBluetooth(void)
{
    uint8_t ch;

    while (Bluetooth_ReadByte(&ch))
    {
        switch (ch)
        {
        case '1':
            g_brush_on = 1;
            s_actuator_dirty = 1;
            break;

        case '0':
            g_brush_on = 0;
            s_actuator_dirty = 1;
            break;

        case '+':
            g_brush_speed += 10;
            if (g_brush_speed > 100) g_brush_speed = 100;
            if (g_brush_on) s_actuator_dirty = 1;
            break;

        case '-':
            g_brush_speed -= 10;
            if (g_brush_speed < 0) g_brush_speed = 0;
            if (g_brush_on) s_actuator_dirty = 1;
            break;

        case 'F':
        case 'f':
            g_fan_on ^= 1;
            s_actuator_dirty = 1;
            break;

        case 'W':
        case 'w':
            g_mist_on ^= 1;
            s_actuator_dirty = 1;
            break;

        default:
            break;
        }
    }
}

/* ==================== 蓝牙数据上报 (使用静态缓冲区) ==================== */

void Menu_SendBluetoothData(void)
{
    /*
     * 格式: "M:x SPD:xxx PM25:xxxxx PM10:xxxxx WL:xxx% FAN:x MIST:x\r\n"
     * 使用手动拼接代替 snprintf，避免标准库栈消耗
     */
    char *p = s_bt_tx_buf;
    uint8_t len;

    /* M: */
    *p++ = 'M'; *p++ = ':';
    *p++ = g_brush_on ? '1' : '0';
    *p++ = ' ';

    /* SPD: */
    *p++ = 'S'; *p++ = 'P'; *p++ = 'D'; *p++ = ':';
    len = i32_to_str(p, g_brush_speed);
    p += len;
    *p++ = ' ';

    /* PM25: */
    *p++ = 'P'; *p++ = 'M'; *p++ = '2'; *p++ = '5'; *p++ = ':';
    len = u16_to_str(p, g_pm25);
    p += len;
    *p++ = ' ';

    /* PM10: */
    *p++ = 'P'; *p++ = 'M'; *p++ = '1'; *p++ = '0'; *p++ = ':';
    len = u16_to_str(p, g_pm10);
    p += len;
    *p++ = ' ';

    /* WL: */
    *p++ = 'W'; *p++ = 'L'; *p++ = ':';
    len = u16_to_str(p, g_water_percent);
    p += len;
    *p++ = '%'; *p++ = ' ';

    /* FAN: */
    *p++ = 'F'; *p++ = 'A'; *p++ = 'N'; *p++ = ':';
    *p++ = g_fan_on ? '1' : '0';
    *p++ = ' ';

    /* MIST: */
    *p++ = 'M'; *p++ = 'I'; *p++ = 'S'; *p++ = 'T'; *p++ = ':';
    *p++ = g_mist_on ? '1' : '0';

    /* 行尾 */
    *p++ = '\r'; *p++ = '\n'; *p++ = '\0';

    Bluetooth_SendString(s_bt_tx_buf);
}
