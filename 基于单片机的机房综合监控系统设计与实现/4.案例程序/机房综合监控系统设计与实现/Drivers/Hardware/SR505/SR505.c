#include ".\Hardware\SR505\SR505.h"

static uint8_t s_lastLevel = 0u;
static uint8_t s_motionStartEvent = 0u;
static uint8_t s_motionStopEvent = 0u;

static void SR505_GPIO_ClockEnable(void)
{
	if (SR505_GPIO_PORT == GPIOA)
	{
		__HAL_RCC_GPIOA_CLK_ENABLE();
	}
	else if (SR505_GPIO_PORT == GPIOB)
	{
		__HAL_RCC_GPIOB_CLK_ENABLE();
	}
	else if (SR505_GPIO_PORT == GPIOC)
	{
		__HAL_RCC_GPIOC_CLK_ENABLE();
	}
	else if (SR505_GPIO_PORT == GPIOD)
	{
		__HAL_RCC_GPIOD_CLK_ENABLE();
	}
	else if (SR505_GPIO_PORT == GPIOE)
	{
		__HAL_RCC_GPIOE_CLK_ENABLE();
	}
}

void SR505_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	SR505_GPIO_ClockEnable();

	GPIO_InitStruct.Pin = SR505_GPIO_PIN;
	GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	/* SR505 idle level is low. Pull-down prevents false-high when the line is
	   not actively driven during power-up or if the signal is unstable. */
	GPIO_InitStruct.Pull = GPIO_PULLDOWN;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(SR505_GPIO_PORT, &GPIO_InitStruct);

	s_lastLevel = SR505_GetLevel();
	s_motionStartEvent = 0u;
	s_motionStopEvent = 0u;
}

uint8_t SR505_GetLevel(void)
{
	return (HAL_GPIO_ReadPin(SR505_GPIO_PORT, SR505_GPIO_PIN) == GPIO_PIN_SET) ? 1u : 0u;
}

SR505_State_t SR505_GetState(void)
{
	return (SR505_GetLevel() == 1u) ? SR505_MOTION_ACTIVE : SR505_NO_MOTION;
}

uint8_t SR505_IsMotionDetected(void)
{
	return SR505_GetLevel();
}

void SR505_Update(void)
{
	uint8_t curLevel = SR505_GetLevel();

	if ((curLevel == 1u) && (s_lastLevel == 0u))
	{
		s_motionStartEvent = 1u;
	}
	else if ((curLevel == 0u) && (s_lastLevel == 1u))
	{
		s_motionStopEvent = 1u;
	}

	s_lastLevel = curLevel;
}

uint8_t SR505_GetMotionStartEvent(void)
{
	uint8_t event = s_motionStartEvent;
	s_motionStartEvent = 0u;
	return event;
}

uint8_t SR505_GetMotionStopEvent(void)
{
	uint8_t event = s_motionStopEvent;
	s_motionStopEvent = 0u;
	return event;
}

