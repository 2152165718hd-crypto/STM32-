#include ".\Hardware\TB6612\TB6612.h"

static TIM_HandleTypeDef *s_htimA = NULL;
static uint32_t s_chA = 0u;
static TIM_HandleTypeDef *s_htimB = NULL;
static uint32_t s_chB = 0u;
static uint8_t s_motorA_active = 0u;
static uint8_t s_motorB_active = 0u;

static void TB6612_SetStandby(uint8_t enable)
{
#if TB6612_USE_STBY_CTRL
    HAL_GPIO_WritePin(TB6612_STBY_PORT, TB6612_STBY_PIN, enable ? GPIO_PIN_SET : GPIO_PIN_RESET);
#else
    (void)enable;
#endif
}

static void TB6612_WriteDirection(GPIO_TypeDef *portA, uint16_t pinA, GPIO_TypeDef *portB, uint16_t pinB, TB6612_Dir_t dir)
{
    switch (dir)
    {
        case TB6612_DIR_FORWARD:
            HAL_GPIO_WritePin(portA, pinA, GPIO_PIN_SET);
            HAL_GPIO_WritePin(portB, pinB, GPIO_PIN_RESET);
            break;
        case TB6612_DIR_BACKWARD:
            HAL_GPIO_WritePin(portA, pinA, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(portB, pinB, GPIO_PIN_SET);
            break;
        case TB6612_DIR_BRAKE:
            HAL_GPIO_WritePin(portA, pinA, GPIO_PIN_SET);
            HAL_GPIO_WritePin(portB, pinB, GPIO_PIN_SET);
            break;
        case TB6612_DIR_COAST:
        default:
            HAL_GPIO_WritePin(portA, pinA, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(portB, pinB, GPIO_PIN_RESET);
            break;
    }
}

static void TB6612_SetPwmLevel(GPIO_TypeDef *port, uint16_t pin, TIM_HandleTypeDef *htim, uint32_t channel, uint8_t speed_percent)
{
    if (speed_percent == 0u)
    {
        HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET);
        if (htim != NULL)
        {
            __HAL_TIM_SET_COMPARE(htim, channel, 0u);
        }
        return;
    }

    if (htim != NULL)
    {
        uint32_t arr = __HAL_TIM_GET_AUTORELOAD(htim) + 1u;
        uint32_t pulse = ((uint32_t)speed_percent * arr) / 100u;
        __HAL_TIM_SET_COMPARE(htim, channel, pulse);
    }
    else
    {
        HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET);
    }
}

void TB6612_Init(void)
{
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* 释放 PB3/PB4，避免 JTAG 占用导致 TB6612 B 路失效 */
    __HAL_AFIO_REMAP_SWJ_NOJTAG();

    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Pin = TB6612_PWMA_PIN | TB6612_AIN1_PIN | TB6612_AIN2_PIN |
                          TB6612_BIN1_PIN | TB6612_BIN2_PIN | TB6612_PWMB_PIN;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

#if TB6612_USE_STBY_CTRL
    GPIO_InitStruct.Pin = TB6612_STBY_PIN;
    HAL_GPIO_Init(TB6612_STBY_PORT, &GPIO_InitStruct);
#endif

    TB6612_SetStandby(0u);
    TB6612_StopAll();
}

void TB6612_ConfigPwm(TIM_HandleTypeDef *htimA, uint32_t chA, TIM_HandleTypeDef *htimB, uint32_t chB)
{
    s_htimA = htimA;
    s_chA = chA;
    s_htimB = htimB;
    s_chB = chB;

    if (s_htimA != NULL)
    {
        HAL_TIM_PWM_Start(s_htimA, s_chA);
    }
    if (s_htimB != NULL)
    {
        HAL_TIM_PWM_Start(s_htimB, s_chB);
    }
}

void TB6612_SetMotor(TB6612_Motor_t motor, TB6612_Dir_t dir, uint8_t speed_percent)
{
    if (speed_percent > 100u)
    {
        speed_percent = 100u;
    }

    if (speed_percent == 0u && dir != TB6612_DIR_BRAKE)
    {
        dir = TB6612_DIR_COAST;
    }

    if (motor == TB6612_MOTOR_A)
    {
        s_motorA_active = (dir != TB6612_DIR_COAST) ? 1u : 0u;
    }
    else
    {
        s_motorB_active = (dir != TB6612_DIR_COAST) ? 1u : 0u;
    }

    TB6612_SetStandby((s_motorA_active || s_motorB_active) ? 1u : 0u);

    if (motor == TB6612_MOTOR_A)
    {
        TB6612_WriteDirection(TB6612_AIN1_PORT, TB6612_AIN1_PIN,
                              TB6612_AIN2_PORT, TB6612_AIN2_PIN,
                              dir);
        TB6612_SetPwmLevel(TB6612_PWMA_PORT, TB6612_PWMA_PIN, s_htimA, s_chA, speed_percent);
    }
    else
    {
        TB6612_WriteDirection(TB6612_BIN1_PORT, TB6612_BIN1_PIN,
                              TB6612_BIN2_PORT, TB6612_BIN2_PIN,
                              dir);
        TB6612_SetPwmLevel(TB6612_PWMB_PORT, TB6612_PWMB_PIN, s_htimB, s_chB, speed_percent);
    }
}

void TB6612_StopMotor(TB6612_Motor_t motor)
{
    TB6612_SetMotor(motor, TB6612_DIR_COAST, 0u);
}

void TB6612_StopAll(void)
{
    TB6612_SetMotor(TB6612_MOTOR_A, TB6612_DIR_COAST, 0u);
    TB6612_SetMotor(TB6612_MOTOR_B, TB6612_DIR_COAST, 0u);
    s_motorA_active = 0u;
    s_motorB_active = 0u;
    TB6612_SetStandby(0u);
}
