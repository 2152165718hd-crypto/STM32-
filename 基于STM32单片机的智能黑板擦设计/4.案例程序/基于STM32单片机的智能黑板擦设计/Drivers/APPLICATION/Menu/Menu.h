#ifndef __MENU_H
#define __MENU_H

#include "stm32f1xx_hal.h"
#include <stdint.h>

/* ==================== 应用层全局变量 (extern) ==================== */

/* 传感器数据 */
extern uint16_t g_pm25;            /* PM2.5 浓度 (μg/m³)         */
extern uint16_t g_pm10;            /* PM10  浓度 (μg/m³)         */
extern uint8_t  g_water_percent;   /* 水位百分比 (0~100)         */

/* 阈值设置 */
extern int32_t  g_dust_threshold;  /* 灰尘浓度阈值 (μg/m³)      */
extern int32_t  g_water_threshold; /* 水位报警阈值 (%)           */

/* 毛刷/电机转速 */
extern int32_t  g_brush_speed;     /* 毛刷转速百分比 (0~100)     */

/* 功能开关 */
extern uint8_t  g_fan_on;          /* 风扇开关   0/1             */
extern uint8_t  g_mist_on;         /* 喷水开关   0/1             */
extern uint8_t  g_brush_on;        /* 毛刷开关   0/1             */

/* 自动清洁状态 */
extern uint8_t  g_auto_cleaning;   /* 自动清洁中标志             */

/* ==================== 应用层函数声明 ==================== */

/**
 * @brief  初始化菜单树 (创建全部菜单页和条目)
 * @note   必须在所有硬件 Init 之后、MF_Start 之前调用
 */
void Menu_Init(void);

/**
 * @brief  自动控制逻辑 (灰尘超阈值 → 开启清洁; 水位不足 → 停喷水)
 * @note   在主循环中定时调用 (建议 200ms)
 */
void Menu_AutoControl(void);

/**
 * @brief  处理蓝牙模块接收到的命令
 * @note   在主循环中持续调用
 */
void Menu_HandleBluetooth(void);

/**
 * @brief  手动切换毛刷状态（含自动模式下的手动覆盖处理）
 */
void Menu_ToggleBrush(void);

/**
 * @brief  通过蓝牙上报当前状态数据
 * @note   在主循环中定时调用 (建议 1000ms)
 */
void Menu_SendBluetoothData(void);

/**
 * @brief  请求执行器下次循环强制同步
 */
void Menu_RequestActuatorSync(void);

/**
 * @brief  执行器状态同步 (根据 g_xxx_on 变量控制硬件)
 * @note   在主循环每轮调用，确保硬件状态与变量一致
 */
void Menu_ApplyActuators(void);

#endif /* __MENU_H */
