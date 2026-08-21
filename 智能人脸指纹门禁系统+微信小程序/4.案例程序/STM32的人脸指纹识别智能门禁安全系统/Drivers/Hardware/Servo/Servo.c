#include "./Hardware/Servo/Servo.h"

#define SERVO_TIMER TIM2
#define SERVO_TIMER_CHANNEL TIM_CHANNEL_1

#define SERVO_PWM_FREQ_HZ 50U
#define SERVO_TIMER_CLK_HZ 72000000U
#define SERVO_TICK_HZ 1000000U

#define SERVO_MAX_ANGLE 180U
#define SERVO_DEFAULT_ANGLE 0U

#define SERVO_MIN_PULSE_US 500U
#define SERVO_MAX_PULSE_US 2500U

static TIM_HandleTypeDef htim2;
static uint8_t servo_inited = 0U;

static void Servo_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_AFIO_CLK_ENABLE();

    GPIO_InitStruct.Pin = SERVO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(SERVO_GPIO_PORT, &GPIO_InitStruct);
}

static uint8_t Servo_TIM_Init(void)
{
    TIM_OC_InitTypeDef sConfigOC = {0};

    __HAL_RCC_TIM2_CLK_ENABLE();

    htim2.Instance = SERVO_TIMER;
    htim2.Init.Prescaler = (SERVO_TIMER_CLK_HZ / SERVO_TICK_HZ) - 1U;
    htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim2.Init.Period = (SERVO_TICK_HZ / SERVO_PWM_FREQ_HZ) - 1U;
    htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_PWM_Init(&htim2) != HAL_OK)
    {
        return 0U;
    }

    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 1500U;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, SERVO_TIMER_CHANNEL) != HAL_OK)
    {
        return 0U;
    }

    if (HAL_TIM_PWM_Start(&htim2, SERVO_TIMER_CHANNEL) != HAL_OK)
    {
        return 0U;
    }

    return 1U;
}

void Servo_Init(void)
{
    Servo_GPIO_Init();
    if (Servo_TIM_Init() == 0U)
    {
        servo_inited = 0U;
        return;
    }
    servo_inited = 1U;
    Servo_SetAngle(SERVO_DEFAULT_ANGLE);
}

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
    __HAL_TIM_SET_COMPARE(&htim2, SERVO_TIMER_CHANNEL, pulse);
}
