#ifndef __HC_SR04_H
#define __HC_SR04_H

/**
 * @file HC_SR04.h
 * @brief HC-SR04 超声波测距模块驱动接口。
 */

#include "stm32f1xx_hal.h"
#include ".\SYSTEM\delay\delay.h"

/* ==================== 引脚定义 ==================== */
#define HC_SR04_Trig_PORT GPIOA
#define HC_SR04_Trig_PIN GPIO_PIN_12
#define HC_SR04_Echo_PORT GPIOA
#define HC_SR04_Echo_PIN GPIO_PIN_15

/* ==================== 参数配置 ==================== */
#define HC_SR04_ECHO_TIMEOUT_US 30000U
#define HC_SR04_DISABLE_JTAG_IF_NEEDED 1U

/* ==================== 状态码定义 ==================== */
typedef enum
{
	HC_SR04_OK = 0U,
	HC_SR04_ERR_PARAM = 1U,
	HC_SR04_ERR_TIMEOUT_WAIT_HIGH = 2U,
	HC_SR04_ERR_TIMEOUT_WAIT_LOW = 3U,
	HC_SR04_ERR_NOT_INIT = 4U
} HC_SR04_Status_t;

/* ==================== 函数声明 ==================== */
/**
 * @brief 初始化 HC-SR04 引脚。
 */
void HC_SR04_Init(void);

/**
 * @brief 读取一次 Echo 高电平持续时间。
 * @param echo_time_us 返回 Echo 持续时间，单位 us。
 * @return HC_SR04_Status_t 状态码。
 */
HC_SR04_Status_t HC_SR04_ReadEchoTimeUs(uint32_t *echo_time_us);

/**
 * @brief 读取一次距离，单位 cm。
 * @param distance_cm 返回测得距离，单位 cm。
 * @return HC_SR04_Status_t 状态码。
 */
HC_SR04_Status_t HC_SR04_ReadDistanceCm(float *distance_cm);

/**
 * @brief 读取一次距离，单位 mm。
 * @param distance_mm 返回测得距离，单位 mm。
 * @return HC_SR04_Status_t 状态码。
 */
HC_SR04_Status_t HC_SR04_ReadDistanceMm(uint16_t *distance_mm);

#endif /* __HC_SR04_H */
