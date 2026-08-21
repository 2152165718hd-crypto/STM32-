#include "Hardware/Motor/Motor.h"

#define MOTOR_PWM_PERIOD 3599u

static TIM_HandleTypeDef g_motor_tim2;
static TIM_HandleTypeDef g_motor_tim3;
static uint8_t g_motor_inited = 0u;
static int8_t g_left_speed = 0;
static int8_t g_right_speed = 0;

static int8_t Motor_ApplyLeftPolarity(int8_t speed)
{
#if (MOTOR_LEFT_INVERT != 0u)
    return (int8_t)(-speed);
#else
    return speed;
#endif
}

static int8_t Motor_ApplyRightPolarity(int8_t speed)
{
#if (MOTOR_RIGHT_INVERT != 0u)
    return (int8_t)(-speed);
#else
    return speed;
#endif
}

static void Motor_GPIO_Init(void)
{
    GPIO_InitTypeDef gpio_init = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_AFIO_CLK_ENABLE();

    gpio_init.Mode = GPIO_MODE_AF_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;

    gpio_init.Pin = MOTOR_PWMA_PIN;
    HAL_GPIO_Init(MOTOR_PWMA_GPIO_PORT, &gpio_init);

    gpio_init.Pin = MOTOR_PWMB_PIN;
    HAL_GPIO_Init(MOTOR_PWMB_GPIO_PORT, &gpio_init);

    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pin = MOTOR_AIN1_PIN | MOTOR_AIN2_PIN | MOTOR_BIN1_PIN | MOTOR_BIN2_PIN;
    HAL_GPIO_Init(MOTOR_IN_GPIO_PORT, &gpio_init);

    HAL_GPIO_WritePin(MOTOR_IN_GPIO_PORT,
                      MOTOR_AIN1_PIN | MOTOR_AIN2_PIN | MOTOR_BIN1_PIN | MOTOR_BIN2_PIN,
                      GPIO_PIN_RESET);
}

static void Motor_TIM2_Init(void)
{
    TIM_OC_InitTypeDef oc_init = {0};

    __HAL_RCC_TIM2_CLK_ENABLE();

    g_motor_tim2.Instance = TIM2;
    g_motor_tim2.Init.Prescaler = 0u;
    g_motor_tim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    g_motor_tim2.Init.Period = MOTOR_PWM_PERIOD;
    g_motor_tim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    g_motor_tim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_TIM_PWM_Init(&g_motor_tim2);

    oc_init.OCMode = TIM_OCMODE_PWM1;
    oc_init.Pulse = 0u;
    oc_init.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc_init.OCFastMode = TIM_OCFAST_DISABLE;
    HAL_TIM_PWM_ConfigChannel(&g_motor_tim2, &oc_init, TIM_CHANNEL_4);
    HAL_TIM_PWM_Start(&g_motor_tim2, TIM_CHANNEL_4);
}

static void Motor_TIM3_Init(void)
{
    TIM_OC_InitTypeDef oc_init = {0};

    __HAL_RCC_TIM3_CLK_ENABLE();

    g_motor_tim3.Instance = TIM3;
    g_motor_tim3.Init.Prescaler = 0u;
    g_motor_tim3.Init.CounterMode = TIM_COUNTERMODE_UP;
    g_motor_tim3.Init.Period = MOTOR_PWM_PERIOD;
    g_motor_tim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    g_motor_tim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_TIM_PWM_Init(&g_motor_tim3);

    oc_init.OCMode = TIM_OCMODE_PWM1;
    oc_init.Pulse = 0u;
    oc_init.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc_init.OCFastMode = TIM_OCFAST_DISABLE;
    HAL_TIM_PWM_ConfigChannel(&g_motor_tim3, &oc_init, TIM_CHANNEL_3);
    HAL_TIM_PWM_Start(&g_motor_tim3, TIM_CHANNEL_3);
}

static uint16_t Motor_SpeedToPwm(int8_t speed)
{
    uint32_t magnitude = (uint32_t)((speed >= 0) ? speed : -speed);
    return (uint16_t)((magnitude * (MOTOR_PWM_PERIOD + 1u)) / 100u);
}

static void Motor_SetDirectionA(int8_t speed)
{
    if (speed > 0)
    {
        HAL_GPIO_WritePin(MOTOR_IN_GPIO_PORT, MOTOR_AIN1_PIN, GPIO_PIN_SET);
        HAL_GPIO_WritePin(MOTOR_IN_GPIO_PORT, MOTOR_AIN2_PIN, GPIO_PIN_RESET);
    }
    else if (speed < 0)
    {
        HAL_GPIO_WritePin(MOTOR_IN_GPIO_PORT, MOTOR_AIN1_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(MOTOR_IN_GPIO_PORT, MOTOR_AIN2_PIN, GPIO_PIN_SET);
    }
    else
    {
        HAL_GPIO_WritePin(MOTOR_IN_GPIO_PORT, MOTOR_AIN1_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(MOTOR_IN_GPIO_PORT, MOTOR_AIN2_PIN, GPIO_PIN_RESET);
    }
}

static void Motor_SetDirectionB(int8_t speed)
{
    if (speed > 0)
    {
        HAL_GPIO_WritePin(MOTOR_IN_GPIO_PORT, MOTOR_BIN1_PIN, GPIO_PIN_SET);
        HAL_GPIO_WritePin(MOTOR_IN_GPIO_PORT, MOTOR_BIN2_PIN, GPIO_PIN_RESET);
    }
    else if (speed < 0)
    {
        HAL_GPIO_WritePin(MOTOR_IN_GPIO_PORT, MOTOR_BIN1_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(MOTOR_IN_GPIO_PORT, MOTOR_BIN2_PIN, GPIO_PIN_SET);
    }
    else
    {
        HAL_GPIO_WritePin(MOTOR_IN_GPIO_PORT, MOTOR_BIN1_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(MOTOR_IN_GPIO_PORT, MOTOR_BIN2_PIN, GPIO_PIN_RESET);
    }
}

void Motor_Init(void)
{
    Motor_GPIO_Init();
    Motor_TIM2_Init();
    Motor_TIM3_Init();
    g_motor_inited = 1u;
    Motor_Stop();
}

void Motor_SetRightSpeed(int8_t speed)
{
    int8_t effective_speed;

    if (!g_motor_inited)
    {
        return;
    }

    if (speed > 100)
    {
        speed = 100;
    }
    else if (speed < -100)
    {
        speed = -100;
    }

    g_right_speed = speed;
    effective_speed = Motor_ApplyRightPolarity(speed);
    Motor_SetDirectionA(effective_speed);
    __HAL_TIM_SET_COMPARE(&g_motor_tim2, TIM_CHANNEL_4, Motor_SpeedToPwm(effective_speed));
}

void Motor_SetLeftSpeed(int8_t speed)
{
    int8_t effective_speed;

    if (!g_motor_inited)
    {
        return;
    }

    if (speed > 100)
    {
        speed = 100;
    }
    else if (speed < -100)
    {
        speed = -100;
    }

    g_left_speed = speed;
    effective_speed = Motor_ApplyLeftPolarity(speed);
    Motor_SetDirectionB(effective_speed);
    __HAL_TIM_SET_COMPARE(&g_motor_tim3, TIM_CHANNEL_3, Motor_SpeedToPwm(effective_speed));
}

void Motor_SetDualSpeed(int8_t left_speed, int8_t right_speed)
{
    Motor_SetLeftSpeed(left_speed);
    Motor_SetRightSpeed(right_speed);
}

void Motor_SetAllSpeed(int8_t speed)
{
    Motor_SetDualSpeed(speed, speed);
}

void Motor_Stop(void)
{
    Motor_SetDualSpeed(0, 0);
}

uint8_t Motor_IsRunning(void)
{
    return (uint8_t)((g_left_speed != 0) || (g_right_speed != 0));
}
