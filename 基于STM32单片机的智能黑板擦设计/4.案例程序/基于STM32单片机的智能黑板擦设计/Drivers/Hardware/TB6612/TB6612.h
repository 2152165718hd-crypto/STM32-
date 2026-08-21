#ifndef __TB6612_H
#define __TB6612_H

#include "stm32f1xx_hal.h"

/* TB6612 引脚定义 */
#define TB6612_PWMA_PORT GPIOB
#define TB6612_PWMA_PIN  GPIO_PIN_8

#define TB6612_AIN1_PORT GPIOB
#define TB6612_AIN1_PIN  GPIO_PIN_6

#define TB6612_AIN2_PORT GPIOB
#define TB6612_AIN2_PIN  GPIO_PIN_7

#define TB6612_BIN1_PORT GPIOB
#define TB6612_BIN1_PIN  GPIO_PIN_5

#define TB6612_BIN2_PORT GPIOB
#define TB6612_BIN2_PIN  GPIO_PIN_4

#define TB6612_PWMB_PORT GPIOB
#define TB6612_PWMB_PIN  GPIO_PIN_3

/*
 * TB6612 STBY 引脚：默认使用 PB9。
 * 如果你的模块将 STBY 直接上拉到 3.3V，可将 TB6612_USE_STBY_CTRL 改为 0。
 */
#define TB6612_USE_STBY_CTRL 0
#define TB6612_STBY_PORT GPIOB
#define TB6612_STBY_PIN  GPIO_PIN_9

/* 电机通道 */
typedef enum
{
    TB6612_MOTOR_A = 0,
    TB6612_MOTOR_B = 1
} TB6612_Motor_t;

/* 运行方向 */
typedef enum
{
    TB6612_DIR_COAST = 0,    /* 空转 */
    TB6612_DIR_FORWARD,      /* 正转 */
    TB6612_DIR_BACKWARD,     /* 反转 */
    TB6612_DIR_BRAKE         /* 刹车 */
} TB6612_Dir_t;

/* 初始化 TB6612 引脚 */
void TB6612_Init(void);

/*
 * 配置硬件 PWM 定时器句柄。
 * 使用此函数后，模块会使用 TIM 产生占空比；
 * 如果不调用，则 PWM 引脚使用数字输出。
 */
void TB6612_ConfigPwm(TIM_HandleTypeDef *htimA, uint32_t chA, TIM_HandleTypeDef *htimB, uint32_t chB);

/* 控制指定电机，speed_percent 范围 0~100 */
void TB6612_SetMotor(TB6612_Motor_t motor, TB6612_Dir_t dir, uint8_t speed_percent);

/* 停止指定电机 */
void TB6612_StopMotor(TB6612_Motor_t motor);

/* 停止所有电机 */
void TB6612_StopAll(void);

#endif /* __TB6612_H */
