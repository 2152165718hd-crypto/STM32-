#ifndef __ACTUATORS_H__
#define __ACTUATORS_H__

/**
 * @file Actuators.h
 * @brief 执行器驱动接口声明。
 */

#include "stm32f1xx_hal.h"
#include ".\SYSTEM\delay\delay.h"

/* ==================== 引脚定义 ==================== */
#define WaterPump_IN1_GPIO_PORT GPIOA
#define WaterPump_IN1_GPIO_PIN GPIO_PIN_2
#define WaterPump_IN2_GPIO_PORT GPIOA
#define WaterPump_IN2_GPIO_PIN GPIO_PIN_3

#define Humidifier_GPIO_PORT GPIOA
#define Humidifier_GPIO_PIN GPIO_PIN_1

#define Fan_GPIO_PORT GPIOB
#define Fan_GPIO_PIN GPIO_PIN_3

#define FILL_LED_GPIO_PORT GPIOA
#define FILL_LED_GPIO_PIN GPIO_PIN_0

/* ==================== 逻辑电平配置 ==================== */
/* 若模块为低电平有效，请改为 GPIO_PIN_RESET。 */
#define WaterPump_IN1_ACTIVE_LEVEL GPIO_PIN_SET
#define WaterPump_IN2_ACTIVE_LEVEL GPIO_PIN_SET
#define Humidifier_ACTIVE_LEVEL GPIO_PIN_SET
#define Fan_ACTIVE_LEVEL GPIO_PIN_RESET
#define FILL_LED_ACTIVE_LEVEL GPIO_PIN_RESET

/* 水泵换向保护延时，避免正反转瞬时切换。 */
#define WaterPump_SWITCH_DELAY_MS 10U

/* 使用 PB3/PB4/PA15 时可自动释放 JTAG（保留 SWD）。 */
#define ACTUATORS_DISABLE_JTAG_IF_NEEDED 1U

typedef enum
{
	WATERPUMP_STATE_STOP = 0,
	WATERPUMP_STATE_FORWARD,
	WATERPUMP_STATE_REVERSE
} WaterPump_State_t;

/**
 * @brief 初始化执行器 GPIO，并默认全部关闭。
 */
void Actuators_Init(void);

/**
 * @brief 关闭所有执行器（水泵/风扇/加湿器/LED）。
 */
void Actuators_AllOff(void);

/**
 * @brief 设置加湿器开关状态。
 * @param on `1` 打开，`0` 关闭。
 */
void Humidifier_Set(uint8_t on);

/**
 * @brief 打开加湿器。
 */
void Humidifier_On(void);

/**
 * @brief 关闭加湿器。
 */
void Humidifier_Off(void);

/**
 * @brief 设置风扇开关状态。
 * @param on `1` 打开，`0` 关闭。
 */
void Fan_Set(uint8_t on);

/**
 * @brief 打开风扇。
 */
void Fan_On(void);

/**
 * @brief 关闭风扇。
 */
void Fan_Off(void);

/**
 * @brief 设置 LED 开关状态。
 * @param on `1` 打开，`0` 关闭。
 */
void LED_Set(uint8_t on);

/**
 * @brief 打开 LED。
 */
void LED_On(void);

/**
 * @brief 关闭 LED。
 */
void LED_Off(void);

/**
 * @brief 翻转 LED 状态。
 */
void LED_Toggle(void);

/**
 * @brief 设置水泵工作状态。
 * @param state 水泵状态（停止/正转/反转）。
 */
void WaterPump_SetState(WaterPump_State_t state);

/**
 * @brief 水泵正转。
 */
void WaterPump_RunForward(void);

/**
 * @brief 水泵反转。
 */
void WaterPump_RunReverse(void);

/**
 * @brief 停止水泵。
 */
void WaterPump_Stop(void);

/**
 * @brief 获取当前水泵状态。
 * @return WaterPump_State_t 当前状态。
 */
WaterPump_State_t WaterPump_GetState(void);

#endif /* __ACTUATORS_H__ */
