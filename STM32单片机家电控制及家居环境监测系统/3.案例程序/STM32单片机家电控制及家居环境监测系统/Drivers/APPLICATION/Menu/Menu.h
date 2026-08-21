#ifndef __MENU_H
#define __MENU_H

#include ".\APPLICATION\Menu\MenuFramework.h"
#include ".\Hardware\DS18B20\DS18B20.h"
#include ".\Hardware\Sensors\Sensors.h"
#include ".\Hardware\LED\LED.h"
#include ".\Hardware\Relay\Relay.h"
#include ".\Hardware\Servo\Servo.h"
#include ".\Hardware\Motor\Motor.h"
#include ".\Hardware\ESP_01S\ESP_01S.h"
#include ".\SYSTEM\delay\delay.h"
#include <stdio.h>
#include <string.h>

/* ==================== 常量定义 ==================== */

/* 继电器编号 */
#define RELAY_ID_AC 0 /* 空调继电器 */
#define RELAY_ID_RC 1 /* 电饭煲继电器 */

/* 窗户舵机角度 */
#define WINDOW_OPEN_ANGLE 0   /* 开窗角度 */
#define WINDOW_CLOSE_ANGLE 90 /* 关窗角度 */

/* 窗帘电机运行时长 (毫秒) */
#define CURTAIN_MOTOR_DURATION_MS 3000

/* 雨滴传感器下雨判定阈值 (ADC值，低于此值认为下雨) */
#define RAIN_DETECT_THRESHOLD 2000

/* 厕所灯自动亮灯时长 (毫秒) */
#define WASHROOM_LIGHT_DURATION_MS 10000

/* 电饭煲倒计时间隔 (毫秒) */
#define RC_COUNTDOWN_INTERVAL_MS 1000

/* DS18B20 温度采样间隔 (毫秒), 避免750ms阻塞 */
#define DS18B20_SAMPLE_INTERVAL_MS 2000

/* 传感器数据上报间隔 (毫秒) */
#define ESP_UPLOAD_INTERVAL_MS 1000

/* ESP 历史命令最大条数 */
#define ESP_CMD_HISTORY_COUNT 3
#define ESP_CMD_MAX_LEN 20

/* ==================== 函数声明 ==================== */

/**
 * @brief  初始化菜单系统，构建菜单树并启动
 */
void Menu_Init(void);

/**
 * @brief  业务逻辑处理 (在主循环中周期调用)
 *         包含: 传感器自动控制、电饭煲倒计时、ESP数据上报
 */
void Menu_BusinessLogic(void);

/**
 * @brief  ESP 数据接收回调 (注册到 ESP 模块)
 * @param  packet  接收到的数据包
 */
void Menu_OnESPData(ESP_DataPacket_t *packet);

#endif /* __MENU_H */
