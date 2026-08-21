#ifndef __MENU_H
#define __MENU_H

#include "stm32f1xx_hal.h"
#include ".\Hardware\OLED\OLED.h"
#include ".\Hardware\Strobe\Strobe.h"
#include ".\Hardware\DHT11\DHT11.h"
#include ".\Hardware\LightSensor\LightSensor.h"
#include ".\Hardware\KEY\KEY.h"
#include ".\Hardware\JQ8400\JQ8400.h"
#include ".\Hardware\AV_Alarm\AV_Alarm.h"
#include ".\Hardware\DoorWinMagSensor\DoorWinMagSensor.h"
#include ".\Hardware\SR505\SR505.h"
#include ".\APPLICATION\Menu\MenuFramework.h"

/* ==================== 系统状态全局变量 ==================== */

/* 监控模式: 0=OFF, 1=日常监控, 2=离家布防, 3=主动哨兵 */
extern uint8_t  monitor_mode;

/* 温湿度报警阈值 */
extern int32_t  temp_threshold;
extern int32_t  humi_threshold;

/* 报警装置手动开关 */
extern uint8_t  alarm_enabled;
extern uint8_t  strobe_enabled;

/* 报警触发标志（由安全逻辑置位，用于显示） */
extern uint8_t  alarm_triggered;

/* 哨兵模式报警锁定: 0=自动解除, 1=锁定(需手动关闭) */
extern uint8_t  sentry_latch;

/* ==================== 函数声明 ==================== */

/**
 * @brief  初始化菜单系统（构建菜单树并启动）
 *         在所有硬件初始化之后调用
 */
void Menu_Init(void);

/**
 * @brief  菜单主循环（在 while(1) 中调用）
 *         内部执行: 安全监控逻辑 → 菜单框架循环
 */
void Menu_Loop(void);

#endif /* __MENU_H */
