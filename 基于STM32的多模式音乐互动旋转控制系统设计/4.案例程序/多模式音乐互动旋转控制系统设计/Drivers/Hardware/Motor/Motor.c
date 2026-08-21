#include ".\Hardware\Motor\Motor.h"

#define MOTOR_PWM_PRESCALER 7u
#define MOTOR_PWM_PERIOD 999u
#define MOTOR_PWM_COMPARE_MAX MOTOR_PWM_PERIOD
#define MOTOR_CONTROL_FULL_SCALE 1000u
#define MOTOR_MIN_EFFECTIVE_DUTY 120u

static TIM_HandleTypeDef htim2;
static TIM_HandleTypeDef htim3;
static uint8_t motor_inited = 0u;
static uint16_t motor_duty_permille = 0u;

static uint16_t Motor_MapMagnitudeToCompare(uint16_t magnitude_permille)
{
    uint32_t effective_permille;
    uint32_t compare;

    if (magnitude_permille == 0u)
    {
        return 0u;
    }

    if (magnitude_permille > MOTOR_CONTROL_FULL_SCALE)
    {
        magnitude_permille = MOTOR_CONTROL_FULL_SCALE;
    }

    effective_permille = MOTOR_MIN_EFFECTIVE_DUTY +
                         (((uint32_t)(magnitude_permille - 1u) *
                           (MOTOR_CONTROL_FULL_SCALE - MOTOR_MIN_EFFECTIVE_DUTY)) +
                          (MOTOR_CONTROL_FULL_SCALE - 2u)) /
                             (MOTOR_CONTROL_FULL_SCALE - 1u);

    compare = (effective_permille * MOTOR_PWM_COMPARE_MAX + (MOTOR_CONTROL_FULL_SCALE - 1u)) /
              MOTOR_CONTROL_FULL_SCALE;
    if (compare > MOTOR_PWM_COMPARE_MAX)
    {
        compare = MOTOR_PWM_COMPARE_MAX;
    }

    return (uint16_t)compare;
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

    GPIO_InitStruct.Pin = MOTOR_AIN1_PIN | MOTOR_AIN2_PIN | MOTOR_BIN1_PIN | MOTOR_BIN2_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(MOTOR_IN_GPIO_PORT, &GPIO_InitStruct);

    HAL_GPIO_WritePin(MOTOR_IN_GPIO_PORT,
                      MOTOR_AIN1_PIN | MOTOR_AIN2_PIN | MOTOR_BIN1_PIN | MOTOR_BIN2_PIN,
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
    (void)HAL_TIM_PWM_Init(&htim2);

    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 0u;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    (void)HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_4);
    (void)HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_4);
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
    (void)HAL_TIM_PWM_Init(&htim3);

    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 0u;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    (void)HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_3);
    (void)HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);
}

void Motor_Init(void)
{
    Motor_GPIO_Init();
    Motor_TIM2_Init();
    Motor_TIM3_Init();
    motor_inited = 1u;
    Motor_Stop();
}

void Motor_SetRightSpeed(int16_t speed_permille)
{
    uint16_t magnitude_permille;
    uint16_t pwm_value;

    if (motor_inited == 0u)
    {
        return;
    }

    if (speed_permille > 1000)
    {
        speed_permille = 1000;
    }
    if (speed_permille < -1000)
    {
        speed_permille = -1000;
    }

    magnitude_permille = (speed_permille < 0) ? (uint16_t)(-speed_permille) : (uint16_t)speed_permille;
    pwm_value = Motor_MapMagnitudeToCompare(magnitude_permille);

    if (speed_permille > 0)
    {
        HAL_GPIO_WritePin(MOTOR_IN_GPIO_PORT, MOTOR_AIN1_PIN, GPIO_PIN_SET);
        HAL_GPIO_WritePin(MOTOR_IN_GPIO_PORT, MOTOR_AIN2_PIN, GPIO_PIN_RESET);
    }
    else if (speed_permille < 0)
    {
        HAL_GPIO_WritePin(MOTOR_IN_GPIO_PORT, MOTOR_AIN1_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(MOTOR_IN_GPIO_PORT, MOTOR_AIN2_PIN, GPIO_PIN_SET);
    }
    else
    {
        HAL_GPIO_WritePin(MOTOR_IN_GPIO_PORT, MOTOR_AIN1_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(MOTOR_IN_GPIO_PORT, MOTOR_AIN2_PIN, GPIO_PIN_RESET);
    }

    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_4, pwm_value);
}

void Motor_SetLeftSpeed(int16_t speed_permille)
{
    uint16_t magnitude_permille;
    uint16_t pwm_value;

    if (motor_inited == 0u)
    {
        return;
    }

    if (speed_permille > 1000)
    {
        speed_permille = 1000;
    }
    if (speed_permille < -1000)
    {
        speed_permille = -1000;
    }

    magnitude_permille = (speed_permille < 0) ? (uint16_t)(-speed_permille) : (uint16_t)speed_permille;
    pwm_value = Motor_MapMagnitudeToCompare(magnitude_permille);

    if (speed_permille > 0)
    {
        HAL_GPIO_WritePin(MOTOR_IN_GPIO_PORT, MOTOR_BIN1_PIN, GPIO_PIN_SET);
        HAL_GPIO_WritePin(MOTOR_IN_GPIO_PORT, MOTOR_BIN2_PIN, GPIO_PIN_RESET);
    }
    else if (speed_permille < 0)
    {
        HAL_GPIO_WritePin(MOTOR_IN_GPIO_PORT, MOTOR_BIN1_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(MOTOR_IN_GPIO_PORT, MOTOR_BIN2_PIN, GPIO_PIN_SET);
    }
    else
    {
        HAL_GPIO_WritePin(MOTOR_IN_GPIO_PORT, MOTOR_BIN1_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(MOTOR_IN_GPIO_PORT, MOTOR_BIN2_PIN, GPIO_PIN_RESET);
    }

    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, pwm_value);
}

void Motor_SetDutyPermille(uint16_t duty_permille)
{
    if (duty_permille > MOTOR_CONTROL_FULL_SCALE)
    {
        duty_permille = MOTOR_CONTROL_FULL_SCALE;
    }

    Motor_SetLeftSpeed((int16_t)duty_permille);
    Motor_SetRightSpeed((int16_t)duty_permille);
    motor_duty_permille = duty_permille;
}

void Motor_SetPercent(uint8_t percent)
{
    if (percent > 100u)
    {
        percent = 100u;
    }

    Motor_SetDutyPermille((uint16_t)percent * 10u);
}

void Motor_Stop(void)
{
    Motor_SetLeftSpeed(0);
    Motor_SetRightSpeed(0);
    motor_duty_permille = 0u;
}

uint16_t Motor_GetDutyPermille(void)
{
    return motor_duty_permille;
}

uint8_t Motor_GetDutyPercent(void)
{
    return (uint8_t)((motor_duty_permille + 5u) / 10u);
}
