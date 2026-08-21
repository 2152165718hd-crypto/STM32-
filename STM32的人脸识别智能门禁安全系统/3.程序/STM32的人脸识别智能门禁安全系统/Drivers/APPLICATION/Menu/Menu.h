#ifndef __MENU_H
#define __MENU_H

#include "stm32f1xx_hal.h"
#include ".\APPLICATION\Menu\MenuFramework.h"
#include ".\Hardware\FM225\FM225.h"
#include ".\Hardware\RC522\RC522.h"
#include ".\Hardware\ESP_01S\ESP_01S.h"
#include ".\Hardware\SR505\SR505.h"
#include ".\Hardware\LightSensor\LightSensor.h"
#include ".\Hardware\Buzzer\Buzzer.h"
#include ".\Hardware\LED\LED.h"
#include ".\Hardware\Servo\Servo.h"
#include ".\SYSTEM\delay\delay.h"
#include <stdio.h>
#include <string.h>

/* ==================== 应用配置 ==================== */
#define APP_LIGHT_THRESHOLD_DEFAULT 1500 /* 补光灯光照阈值默认值(ADC) */
#define APP_DOOR_OPEN_ANGLE 90           /* 舵机开门角度 */
#define APP_DOOR_CLOSE_ANGLE 0           /* 舵机关门角度 */
#define APP_DOOR_OPEN_TIME 3000          /* 开门持续时间 ms */
#define APP_BUZZER_ERR_TIME 3000         /* 错误蜂鸣器持续时间 ms */
#define APP_BUZZER_ERR_PERIOD 300        /* 错误蜂鸣器滴滴周期 ms */
#define APP_ESP_HISTORY_MAX 3            /* ESP历史命令最大条数 */
#define APP_ESP_CMD_LEN 32               /* ESP单条命令最大长度 */
#define APP_ESP_UPLOAD_INTERVAL 3000     /* ESP自动上报周期 ms */
#ifndef APP_USE_IWDG
#define APP_USE_IWDG 1 /* 发布版默认开启，调试时可在工程宏中覆盖为0 */
#endif

/* ==================== 人脸录入状态机 ==================== */
typedef enum
{
    ENROLL_IDLE = 0, /* 空闲 */
    ENROLL_MIDDLE,   /* 正面 */
    ENROLL_RIGHT,    /* 右转 */
    ENROLL_LEFT,     /* 左转 */
    ENROLL_DOWN,     /* 低头 */
    ENROLL_UP,       /* 抬头 */
    ENROLL_DONE,     /* 录入完成 */
    ENROLL_FAIL,     /* 录入失败 */
} EnrollState_t;

/* ==================== 人脸识别状态 ==================== */
typedef enum
{
    VERIFY_IDLE = 0,
    VERIFY_WAITING, /* 等待结果 */
    VERIFY_SUCCESS, /* 识别成功 */
    VERIFY_FAIL,    /* 识别失败 */
    VERIFY_ERROR,   /* 识别错误 */
} VerifyState_t;

/* ==================== 公开 API ==================== */

/**
 * @brief  应用层初始化（所有硬件初始化 + 回调注册 + 菜单构建）
 */
void App_Init(void);

/**
 * @brief  应用层主循环（在 while(1) 中调用）
 *         包含: 传感器轮询 + FM225/ESP处理 + 自动逻辑 + 菜单刷新
 */
void App_Loop(void);

#endif /* __MENU_H */
