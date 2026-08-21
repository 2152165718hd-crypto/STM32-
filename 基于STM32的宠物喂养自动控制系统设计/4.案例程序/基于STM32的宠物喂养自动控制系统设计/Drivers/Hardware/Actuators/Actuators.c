#include ".\Hardware\Actuators\Actuators.h"

/**
 * @file Actuators.c
 * @brief 执行器驱动实现（水泵/风扇/加湿器/LED）。
 */

static WaterPump_State_t s_waterpump_state = WATERPUMP_STATE_STOP;

/**
 * @brief 根据端口使能 GPIO 时钟。
 */
static void Actuators_GPIO_Clock_Enable(GPIO_TypeDef *port)
{
	if (port == GPIOA)
	{
		__HAL_RCC_GPIOA_CLK_ENABLE();
	}
	else if (port == GPIOB)
	{
		__HAL_RCC_GPIOB_CLK_ENABLE();
	}
	else if (port == GPIOC)
	{
		__HAL_RCC_GPIOC_CLK_ENABLE();
	}
#if defined(GPIOD)
	else if (port == GPIOD)
	{
		__HAL_RCC_GPIOD_CLK_ENABLE();
	}
#endif
#if defined(GPIOE)
	else if (port == GPIOE)
	{
		__HAL_RCC_GPIOE_CLK_ENABLE();
	}
#endif
}

/**
 * @brief 按有效电平写入执行器引脚。
 */
static void Actuators_WriteByActiveLevel(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState active_level, uint8_t on)
{
	GPIO_PinState target;

	if (on)
	{
		target = active_level;
	}
	else
	{
		target = (active_level == GPIO_PIN_SET) ? GPIO_PIN_RESET : GPIO_PIN_SET;
	}

	HAL_GPIO_WritePin(port, pin, target);
}

void Actuators_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	Actuators_GPIO_Clock_Enable(WaterPump_IN1_GPIO_PORT);
	Actuators_GPIO_Clock_Enable(WaterPump_IN2_GPIO_PORT);
	Actuators_GPIO_Clock_Enable(Humidifier_GPIO_PORT);
	Actuators_GPIO_Clock_Enable(Fan_GPIO_PORT);
	Actuators_GPIO_Clock_Enable(FILL_LED_GPIO_PORT);

#if (ACTUATORS_DISABLE_JTAG_IF_NEEDED)
	__HAL_RCC_AFIO_CLK_ENABLE();
	if (((Fan_GPIO_PORT == GPIOA) && ((Fan_GPIO_PIN & GPIO_PIN_15) != 0U)) ||
		((Fan_GPIO_PORT == GPIOB) && ((Fan_GPIO_PIN & GPIO_PIN_3) != 0U)) ||
		((Fan_GPIO_PORT == GPIOB) && ((Fan_GPIO_PIN & GPIO_PIN_4) != 0U)) ||
		((FILL_LED_GPIO_PORT == GPIOA) && ((FILL_LED_GPIO_PIN & GPIO_PIN_15) != 0U)) ||
		((FILL_LED_GPIO_PORT == GPIOB) && ((FILL_LED_GPIO_PIN & GPIO_PIN_3) != 0U)) ||
		((FILL_LED_GPIO_PORT == GPIOB) && ((FILL_LED_GPIO_PIN & GPIO_PIN_4) != 0U)))
	{
		/* 释放 JTAG 相关引脚，保留 SWD 调试。 */
		__HAL_AFIO_REMAP_SWJ_NOJTAG();
	}
#endif

	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

	GPIO_InitStruct.Pin = WaterPump_IN1_GPIO_PIN;
	HAL_GPIO_Init(WaterPump_IN1_GPIO_PORT, &GPIO_InitStruct);

	GPIO_InitStruct.Pin = WaterPump_IN2_GPIO_PIN;
	HAL_GPIO_Init(WaterPump_IN2_GPIO_PORT, &GPIO_InitStruct);

	GPIO_InitStruct.Pin = Humidifier_GPIO_PIN;
	HAL_GPIO_Init(Humidifier_GPIO_PORT, &GPIO_InitStruct);

	GPIO_InitStruct.Pin = Fan_GPIO_PIN;
	HAL_GPIO_Init(Fan_GPIO_PORT, &GPIO_InitStruct);

	GPIO_InitStruct.Pin = FILL_LED_GPIO_PIN;
	HAL_GPIO_Init(FILL_LED_GPIO_PORT, &GPIO_InitStruct);

	Actuators_AllOff();
}

void Actuators_AllOff(void)
{
	WaterPump_Stop();
	Humidifier_Off();
	Fan_Off();
	LED_Off();
}

void Humidifier_Set(uint8_t on)
{
	Actuators_WriteByActiveLevel(Humidifier_GPIO_PORT, Humidifier_GPIO_PIN, Humidifier_ACTIVE_LEVEL, on);
}

void Humidifier_On(void)
{
	Humidifier_Set(1U);
}

void Humidifier_Off(void)
{
	Humidifier_Set(0U);
}

void Fan_Set(uint8_t on)
{
	Actuators_WriteByActiveLevel(Fan_GPIO_PORT, Fan_GPIO_PIN, Fan_ACTIVE_LEVEL, on);
}

void Fan_On(void)
{
	Fan_Set(1U);
}

void Fan_Off(void)
{
	Fan_Set(0U);
}

void LED_Set(uint8_t on)
{
	Actuators_WriteByActiveLevel(FILL_LED_GPIO_PORT, FILL_LED_GPIO_PIN, FILL_LED_ACTIVE_LEVEL, on);
}

void LED_On(void)
{
	LED_Set(1U);
}

void LED_Off(void)
{
	LED_Set(0U);
}

void LED_Toggle(void)
{
	GPIO_PinState on_state = FILL_LED_ACTIVE_LEVEL;
	GPIO_PinState off_state = (on_state == GPIO_PIN_SET) ? GPIO_PIN_RESET : GPIO_PIN_SET;
	GPIO_PinState current = HAL_GPIO_ReadPin(FILL_LED_GPIO_PORT, FILL_LED_GPIO_PIN);

	HAL_GPIO_WritePin(FILL_LED_GPIO_PORT, FILL_LED_GPIO_PIN, (current == on_state) ? off_state : on_state);
}

void WaterPump_SetState(WaterPump_State_t state)
{
	if (state == WATERPUMP_STATE_FORWARD)
	{
		if (s_waterpump_state == WATERPUMP_STATE_REVERSE)
		{
			WaterPump_Stop();
			delay_ms(WaterPump_SWITCH_DELAY_MS);
		}

		Actuators_WriteByActiveLevel(WaterPump_IN2_GPIO_PORT, WaterPump_IN2_GPIO_PIN, WaterPump_IN2_ACTIVE_LEVEL, 0U);
		Actuators_WriteByActiveLevel(WaterPump_IN1_GPIO_PORT, WaterPump_IN1_GPIO_PIN, WaterPump_IN1_ACTIVE_LEVEL, 1U);
		s_waterpump_state = WATERPUMP_STATE_FORWARD;
	}
	else if (state == WATERPUMP_STATE_REVERSE)
	{
		if (s_waterpump_state == WATERPUMP_STATE_FORWARD)
		{
			WaterPump_Stop();
			delay_ms(WaterPump_SWITCH_DELAY_MS);
		}

		Actuators_WriteByActiveLevel(WaterPump_IN1_GPIO_PORT, WaterPump_IN1_GPIO_PIN, WaterPump_IN1_ACTIVE_LEVEL, 0U);
		Actuators_WriteByActiveLevel(WaterPump_IN2_GPIO_PORT, WaterPump_IN2_GPIO_PIN, WaterPump_IN2_ACTIVE_LEVEL, 1U);
		s_waterpump_state = WATERPUMP_STATE_REVERSE;
	}
	else
	{
		WaterPump_Stop();
	}
}

void WaterPump_RunForward(void)
{
	WaterPump_SetState(WATERPUMP_STATE_FORWARD);
}

void WaterPump_RunReverse(void)
{
	WaterPump_SetState(WATERPUMP_STATE_REVERSE);
}

void WaterPump_Stop(void)
{
	Actuators_WriteByActiveLevel(WaterPump_IN1_GPIO_PORT, WaterPump_IN1_GPIO_PIN, WaterPump_IN1_ACTIVE_LEVEL, 0U);
	Actuators_WriteByActiveLevel(WaterPump_IN2_GPIO_PORT, WaterPump_IN2_GPIO_PIN, WaterPump_IN2_ACTIVE_LEVEL, 0U);
	s_waterpump_state = WATERPUMP_STATE_STOP;
}

WaterPump_State_t WaterPump_GetState(void)
{
	return s_waterpump_state;
}
