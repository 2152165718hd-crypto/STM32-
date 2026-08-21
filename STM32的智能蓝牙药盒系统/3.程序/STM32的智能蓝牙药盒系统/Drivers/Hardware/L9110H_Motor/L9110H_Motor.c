#include ".\Hardware\L9110H_Motor\L9110H_Motor.h"

/**
 * @file L9110H_Motor.c
 * @brief L9110H 电机驱动实现。
 */

/**
 * @brief 初始化电机驱动引脚，并默认停止电机。
 */
void Motor_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    HAL_GPIO_WritePin(MOTOR_INA_PORT, MOTOR_INA_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MOTOR_INB_PORT, MOTOR_INB_PIN, GPIO_PIN_RESET);

    GPIO_InitStruct.Pin = MOTOR_INA_PIN | MOTOR_INB_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

/**
 * @brief 控制电机正转。
 */
void Motor_Forward(void)
{
    HAL_GPIO_WritePin(MOTOR_INA_PORT, MOTOR_INA_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(MOTOR_INB_PORT, MOTOR_INB_PIN, GPIO_PIN_RESET);
}

/**
 * @brief 控制电机反转。
 */
void Motor_Reverse(void)
{
    HAL_GPIO_WritePin(MOTOR_INA_PORT, MOTOR_INA_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MOTOR_INB_PORT, MOTOR_INB_PIN, GPIO_PIN_SET);
}

/**
 * @brief 停止电机。
 */
void Motor_Stop(void)
{
    HAL_GPIO_WritePin(MOTOR_INA_PORT, MOTOR_INA_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MOTOR_INB_PORT, MOTOR_INB_PIN, GPIO_PIN_RESET);
}
