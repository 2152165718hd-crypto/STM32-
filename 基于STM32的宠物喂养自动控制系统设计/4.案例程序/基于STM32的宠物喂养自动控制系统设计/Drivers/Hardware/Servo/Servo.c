#include "./Hardware/Servo/Servo.h"

/**
 * @file Servo.c
 * @brief 舵机 PWM 驱动实现。
 */

#define SERVO_PWM_FREQ_HZ 50U
#define SERVO_TIMER_CLK_HZ 72000000U
#define SERVO_TICK_HZ 1000000U

#define SERVO_MAX_ANGLE 180U
#define SERVO_DEFAULT_ANGLE 0U

#define SERVO_MIN_PULSE_US 500U
#define SERVO_MAX_PULSE_US 2500U

static TIM_HandleTypeDef htim_servo;
static uint8_t servo_inited = 0U;

/**
 * @brief 依据 SERVO_GPIO_PORT 开启对应 GPIO 时钟。
 */
static void Servo_GPIO_Clock_Enable(void)
{
    if (SERVO_GPIO_PORT == GPIOA)
    {
        __HAL_RCC_GPIOA_CLK_ENABLE();
    }
    else if (SERVO_GPIO_PORT == GPIOB)
    {
        __HAL_RCC_GPIOB_CLK_ENABLE();
    }
    else if (SERVO_GPIO_PORT == GPIOC)
    {
        __HAL_RCC_GPIOC_CLK_ENABLE();
    }
#if defined(GPIOD)
    else if (SERVO_GPIO_PORT == GPIOD)
    {
        __HAL_RCC_GPIOD_CLK_ENABLE();
    }
#endif
#if defined(GPIOE)
    else if (SERVO_GPIO_PORT == GPIOE)
    {
        __HAL_RCC_GPIOE_CLK_ENABLE();
    }
#endif
}

/**
 * @brief 依据 SERVO_TIMER 开启对应定时器时钟。
 */
static void Servo_TIM_Clock_Enable(void)
{
    if (SERVO_TIMER == TIM1)
    {
        __HAL_RCC_TIM1_CLK_ENABLE();
    }
    else if (SERVO_TIMER == TIM2)
    {
        __HAL_RCC_TIM2_CLK_ENABLE();
    }
    else if (SERVO_TIMER == TIM3)
    {
        __HAL_RCC_TIM3_CLK_ENABLE();
    }
    else if (SERVO_TIMER == TIM4)
    {
        __HAL_RCC_TIM4_CLK_ENABLE();
    }
}

/**
 * @brief 初始化舵机 PWM 输出引脚。
 */
static void Servo_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    Servo_GPIO_Clock_Enable();
    __HAL_RCC_AFIO_CLK_ENABLE();

    SERVO_TIMER_REMAP_ENABLE();

    GPIO_InitStruct.Pin = SERVO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(SERVO_GPIO_PORT, &GPIO_InitStruct);
}

/**
 * @brief 初始化 TIM3 PWM 参数。
 */
static void Servo_TIM_Init(void)
{
    TIM_OC_InitTypeDef sConfigOC = {0};

    Servo_TIM_Clock_Enable();

    htim_servo.Instance = SERVO_TIMER;
    htim_servo.Init.Prescaler = (SERVO_TIMER_CLK_HZ / SERVO_TICK_HZ) - 1U;
    htim_servo.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim_servo.Init.Period = (SERVO_TICK_HZ / SERVO_PWM_FREQ_HZ) - 1U;
    htim_servo.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim_servo.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_TIM_PWM_Init(&htim_servo);

    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 1500U;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    HAL_TIM_PWM_ConfigChannel(&htim_servo, &sConfigOC, SERVO_TIMER_CHANNEL);

    HAL_TIM_PWM_Start(&htim_servo, SERVO_TIMER_CHANNEL);
}

/**
 * @brief 初始化舵机驱动，并将舵机置于默认角度。
 */
void Servo_Init(void)
{
    Servo_GPIO_Init();
    Servo_TIM_Init();
    servo_inited = 1U;
    Servo_SetAngle(SERVO_DEFAULT_ANGLE);
}

/**
 * @brief 设置舵机角度。
 * @param angle 目标角度，超出范围时会自动钳制到 `0~180`。
 */
void Servo_SetAngle(uint8_t angle)
{
    uint32_t pulse;

    if (!servo_inited)
    {
        return;
    }

    if (angle > SERVO_MAX_ANGLE)
    {
        angle = SERVO_MAX_ANGLE;
    }

    pulse = SERVO_MIN_PULSE_US + ((uint32_t)angle * (SERVO_MAX_PULSE_US - SERVO_MIN_PULSE_US) / SERVO_MAX_ANGLE);
    __HAL_TIM_SET_COMPARE(&htim_servo, SERVO_TIMER_CHANNEL, pulse);
}
