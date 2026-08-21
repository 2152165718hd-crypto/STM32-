#include ".\Hardware\Motor\Motor.h"

/* 定时器句柄 */
static TIM_HandleTypeDef htim2; /* 电机A PWM (PA3 - TIM2_CH4) */
static TIM_HandleTypeDef htim3; /* 电机B PWM (PB0 - TIM3_CH3) */

/* 电机初始化标志 */
static uint8_t motor_inited = 0;

/**
 * @brief  GPIO初始化
 */
static void Motor_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* 使能时钟 */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_AFIO_CLK_ENABLE();

    /* 配置PWM引脚 (复用推挽输出) */
    /* PWMA - PA3 (TIM2_CH4) */
    GPIO_InitStruct.Pin = MOTOR_PWMA_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(MOTOR_PWMA_GPIO_PORT, &GPIO_InitStruct);

    /* PWMB - PB0 (TIM3_CH3) */
    GPIO_InitStruct.Pin = MOTOR_PWMB_PIN;
    HAL_GPIO_Init(MOTOR_PWMB_GPIO_PORT, &GPIO_InitStruct);

    /* 配置方向控制引脚 (通用推挽输出) */
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Pin = MOTOR_AIN1_PIN | MOTOR_AIN2_PIN | MOTOR_BIN1_PIN | MOTOR_BIN2_PIN;
    HAL_GPIO_Init(MOTOR_IN_GPIO_PORT, &GPIO_InitStruct);

    /* 初始化所有方向引脚为低电平(刹车状态) */
    HAL_GPIO_WritePin(MOTOR_IN_GPIO_PORT, MOTOR_AIN1_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MOTOR_IN_GPIO_PORT, MOTOR_AIN2_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MOTOR_IN_GPIO_PORT, MOTOR_BIN1_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MOTOR_IN_GPIO_PORT, MOTOR_BIN2_PIN, GPIO_PIN_RESET);
}

/**
 * @brief  TIM2初始化 (电机A - PWMA)
 */
static void Motor_TIM2_Init(void)
{
    TIM_OC_InitTypeDef sConfigOC = {0};

    __HAL_RCC_TIM2_CLK_ENABLE();

    /* 定时器基本配置
     * 时钟: 72MHz
     * 预分频: 72-1, 得到1MHz计数频率
     * 自动重装载值: 20000-1, 得到50Hz PWM频率
     */
    htim2.Instance = TIM2;
    htim2.Init.Prescaler = 72 - 1;
    htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim2.Init.Period = 20000 - 1;
    htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_PWM_Init(&htim2);

    /* PWM通道4配置 */
    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 0;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_4);

    /* 启动PWM输出 */
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_4);
}

/**
 * @brief  TIM3初始化 (电机B - PWMB)
 */
static void Motor_TIM3_Init(void)
{
    TIM_OC_InitTypeDef sConfigOC = {0};

    __HAL_RCC_TIM3_CLK_ENABLE();

    /* 定时器基本配置
     * 时钟: 72MHz
     * 预分频: 72-1, 得到1MHz计数频率
     * 自动重装载值: 20000-1, 得到50Hz PWM频率，以兼容TIM3的舵机PWM
     */
    htim3.Instance = TIM3;
    htim3.Init.Prescaler = 72 - 1;
    htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim3.Init.Period = 20000 - 1;
    htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_PWM_Init(&htim3);

    /* PWM通道3配置 */
    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 0;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_3);

    /* 启动PWM输出 */
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);
}

/**
 * @brief  电机初始化
 */
void Motor_Init(void)
{
    Motor_GPIO_Init();
    Motor_TIM2_Init();
    Motor_TIM3_Init();
    motor_inited = 1;

    /* 初始状态: 停止 */
    Motor_SetLeftSpeed(0);
    Motor_SetRightSpeed(0);
}

/**
 * @brief  设置右侧电机速度 (电机A)
 * @param  speed: 速度值 -100~100
 *                正值: 正转, 负值: 反转, 0: 刹车
 */
void Motor_SetRightSpeed(int8_t speed)
{
    uint16_t pwm_value;

    if (!motor_inited)
        return;

    /* 限幅 */
    if (speed > 100)
        speed = 100;
    if (speed < -100)
        speed = -100;

    /* 计算PWM占空比 (0-20000) */
    if (speed >= 0)
    {
        pwm_value = (uint16_t)(((uint32_t)speed * 20000) / 100);
    }
    else
    {
        pwm_value = (uint16_t)(((uint32_t)(-speed) * 20000) / 100);
    }

    /* 设置方向和PWM */
    if (speed > 0)
    {
        /* 正转: AIN1=1, AIN2=0 */
        HAL_GPIO_WritePin(MOTOR_IN_GPIO_PORT, MOTOR_AIN1_PIN, GPIO_PIN_SET);
        HAL_GPIO_WritePin(MOTOR_IN_GPIO_PORT, MOTOR_AIN2_PIN, GPIO_PIN_RESET);
    }
    else if (speed < 0)
    {
        /* 反转: AIN1=0, AIN2=1 */
        HAL_GPIO_WritePin(MOTOR_IN_GPIO_PORT, MOTOR_AIN1_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(MOTOR_IN_GPIO_PORT, MOTOR_AIN2_PIN, GPIO_PIN_SET);
    }
    else
    {
        /* 刹车: AIN1=0, AIN2=0 */
        HAL_GPIO_WritePin(MOTOR_IN_GPIO_PORT, MOTOR_AIN1_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(MOTOR_IN_GPIO_PORT, MOTOR_AIN2_PIN, GPIO_PIN_RESET);
    }

    /* 设置PWM占空比 */
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_4, pwm_value);
}

/**
 * @brief  设置左侧电机速度 (电机B)
 * @param  speed: 速度值 -100~100
 *                正值: 正转, 负值: 反转, 0: 刹车
 */
void Motor_SetLeftSpeed(int8_t speed)
{
    uint16_t pwm_value;

    if (!motor_inited)
        return;

    /* 限幅 */
    if (speed > 100)
        speed = 100;
    if (speed < -100)
        speed = -100;

    /* 计算PWM占空比 (0-20000) */
    if (speed >= 0)
    {
        pwm_value = (uint16_t)(((uint32_t)speed * 20000) / 100);
    }
    else
    {
        pwm_value = (uint16_t)(((uint32_t)(-speed) * 20000) / 100);
    }

    /* 设置方向和PWM */
    if (speed > 0)
    {
        /* 正转: BIN1=1, BIN2=0 */
        HAL_GPIO_WritePin(MOTOR_IN_GPIO_PORT, MOTOR_BIN1_PIN, GPIO_PIN_SET);
        HAL_GPIO_WritePin(MOTOR_IN_GPIO_PORT, MOTOR_BIN2_PIN, GPIO_PIN_RESET);
    }
    else if (speed < 0)
    {
        /* 反转: BIN1=0, BIN2=1 */
        HAL_GPIO_WritePin(MOTOR_IN_GPIO_PORT, MOTOR_BIN1_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(MOTOR_IN_GPIO_PORT, MOTOR_BIN2_PIN, GPIO_PIN_SET);
    }
    else
    {
        /* 刹车: BIN1=0, BIN2=0 */
        HAL_GPIO_WritePin(MOTOR_IN_GPIO_PORT, MOTOR_BIN1_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(MOTOR_IN_GPIO_PORT, MOTOR_BIN2_PIN, GPIO_PIN_RESET);
    }

    /* 设置PWM占空比 */
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, pwm_value);
}
