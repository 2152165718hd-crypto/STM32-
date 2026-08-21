#include "./Hardware/Strobe/Strobe.h"

static TIM_HandleTypeDef g_strobe_tim;

static void Strobe_TIM_Base_Init(uint32_t period_ms)
{
    uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
    uint32_t timclk = pclk1;
    uint32_t prescaler;
    uint32_t period;

    if ((RCC->CFGR & RCC_CFGR_PPRE1) != RCC_CFGR_PPRE1_DIV1)
    {
        timclk = pclk1 * 2U;
    }

    /* Use 10 kHz timer tick: 0.1 ms per tick */
    prescaler = (timclk / 10000U) - 1U;
    if (period_ms == 0U)
    {
        period_ms = 1U;
    }
    period = (period_ms * 10U) - 1U;

    g_strobe_tim.Instance = TIM2;
    g_strobe_tim.Init.Prescaler = prescaler;
    g_strobe_tim.Init.CounterMode = TIM_COUNTERMODE_UP;
    g_strobe_tim.Init.Period = period;
    g_strobe_tim.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    g_strobe_tim.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_TIM_Base_Init(&g_strobe_tim);
}

void Strobe_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_TIM2_CLK_ENABLE();

    GPIO_InitStruct.Pin = STROBE_GPIO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(STROBE_GPIO_PORT, &GPIO_InitStruct);

    // 默认关闭 (假设低电平点亮，这里设为高电平)
    HAL_GPIO_WritePin(STROBE_GPIO_PORT, STROBE_GPIO_PIN, GPIO_PIN_SET);

    HAL_NVIC_SetPriority(TIM2_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(TIM2_IRQn);
}

void Strobe_Blink_Start(uint32_t period_ms)
{
    Strobe_TIM_Base_Init(period_ms);
    __HAL_TIM_CLEAR_FLAG(&g_strobe_tim, TIM_FLAG_UPDATE);
    HAL_TIM_Base_Start_IT(&g_strobe_tim);
}

void Strobe_Blink_Stop(void)
{
    HAL_TIM_Base_Stop_IT(&g_strobe_tim);
    // 停止时确保熄灭 (高电平)
    HAL_GPIO_WritePin(STROBE_GPIO_PORT, STROBE_GPIO_PIN, GPIO_PIN_SET);
}

void TIM2_IRQHandler(void)
{
    Strobe_Timer_IRQHandler();
}

void Strobe_Timer_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&g_strobe_tim);
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM2)
    {
        HAL_GPIO_TogglePin(STROBE_GPIO_PORT, STROBE_GPIO_PIN);
    }
}
