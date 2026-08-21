#include ".\Hardware\Motor\Motor.h"

#define MOTOR_PWM_PRESCALER 71u
#define MOTOR_PWM_PERIOD    999u

static TIM_HandleTypeDef htim2;
static TIM_HandleTypeDef htim3;
static uint8_t motor_inited = 0u;

static uint16_t Motor_SpeedToCompare(int8_t speed)
{
    uint32_t abs_speed;

    if (speed < 0)
    {
        abs_speed = (uint32_t)(-speed);
    }
    else
    {
        abs_speed = (uint32_t)speed;
    }

    if (abs_speed > 100u)
    {
        abs_speed = 100u;
    }

    return (uint16_t)((abs_speed * (MOTOR_PWM_PERIOD + 1u)) / 100u);
}

static void Motor_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_AFIO_CLK_ENABLE();

    GPIO_InitStruct.Pin = MOTOR_PWMA_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(MOTOR_PWMA_GPIO_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = MOTOR_PWMB_PIN;
    HAL_GPIO_Init(MOTOR_PWMB_GPIO_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Pin = MOTOR_AIN1_PIN | MOTOR_AIN2_PIN | MOTOR_BIN1_PIN | MOTOR_BIN2_PIN;
    HAL_GPIO_Init(MOTOR_IN_GPIO_PORT, &GPIO_InitStruct);

    HAL_GPIO_WritePin(MOTOR_IN_GPIO_PORT, MOTOR_AIN1_PIN | MOTOR_AIN2_PIN |
                                          MOTOR_BIN1_PIN | MOTOR_BIN2_PIN,
                      GPIO_PIN_RESET);
}

static void Motor_TIM2_Init(void)
{
    TIM_OC_InitTypeDef sConfigOC = {0};

    __HAL_RCC_TIM2_CLK_ENABLE();

    htim2.Instance = TIM2;
    htim2.Init.Prescaler = MOTOR_PWM_PRESCALER;
    htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim2.Init.Period = MOTOR_PWM_PERIOD;
    htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_PWM_Init(&htim2);

    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 0;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_4);
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_4);
}

static void Motor_TIM3_Init(void)
{
    TIM_OC_InitTypeDef sConfigOC = {0};

    __HAL_RCC_TIM3_CLK_ENABLE();

    htim3.Instance = TIM3;
    htim3.Init.Prescaler = MOTOR_PWM_PRESCALER;
    htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim3.Init.Period = MOTOR_PWM_PERIOD;
    htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_PWM_Init(&htim3);

    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 0;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_3);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);
}

void Motor_Init(void)
{
    Motor_GPIO_Init();
    Motor_TIM2_Init();
    Motor_TIM3_Init();
    motor_inited = 1u;
    Motor_StopAll();
}

void Motor_SetRightSpeed(int8_t speed)
{
    uint16_t pwm_value;

    if (motor_inited == 0u)
    {
        return;
    }

    if (speed > 100)
    {
        speed = 100;
    }
    if (speed < -100)
    {
        speed = -100;
    }

    pwm_value = Motor_SpeedToCompare(speed);

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
        HAL_GPIO_WritePin(MOTOR_IN_GPIO_PORT, MOTOR_AIN1_PIN | MOTOR_AIN2_PIN,
                          GPIO_PIN_RESET);
    }

    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_4, pwm_value);
}

void Motor_SetLeftSpeed(int8_t speed)
{
    uint16_t pwm_value;

    if (motor_inited == 0u)
    {
        return;
    }

    if (speed > 100)
    {
        speed = 100;
    }
    if (speed < -100)
    {
        speed = -100;
    }

    pwm_value = Motor_SpeedToCompare(speed);

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
        HAL_GPIO_WritePin(MOTOR_IN_GPIO_PORT, MOTOR_BIN1_PIN | MOTOR_BIN2_PIN,
                          GPIO_PIN_RESET);
    }

    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, pwm_value);
}

void Motor_SetDoorSpeed(int8_t speed)
{
    Motor_SetLeftSpeed(speed);
    Motor_SetRightSpeed(speed);
}

void Motor_StopAll(void)
{
    Motor_SetDoorSpeed(0);
}
