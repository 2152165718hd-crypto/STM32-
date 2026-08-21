#ifndef __MENU_H
#define __MENU_H

#include "stm32f1xx_hal.h"

/* ============ 系统状态枚举 ============ */
typedef enum
{
    SYS_STATE_IDLE = 0,     /* 待机监听 */
    SYS_STATE_DETECTING,    /* AI 识别中 */
    SYS_STATE_ALARMING      /* 报警中 */
} SysState_t;

/* ============ 报警模式枚举 ============ */
typedef enum
{
    ALARM_MODE_SOUND = 0,   /* 声音模式：蜂鸣器 + LED */
    ALARM_MODE_MUTE         /* 静音模式：振动马达 */
} AlarmMode_t;

/* ============ 函数声明 ============ */

/**
 * @brief  菜单/业务逻辑初始化
 *         初始化状态机、OLED 首屏显示、蓝牙模块配置
 */
void Menu_Init(void);

/**
 * @brief  菜单/业务主任务，在主循环中周期调用（建议 100ms）
 *         内部完成：声音检测 → AI 识别 → 报警控制 → 按键处理 → OLED 刷新
 */
void Menu_Task(void);

#endif /* __MENU_H */
