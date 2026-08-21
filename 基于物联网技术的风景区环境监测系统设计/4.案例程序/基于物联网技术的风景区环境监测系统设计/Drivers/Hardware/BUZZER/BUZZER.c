#include ".\Hardware\BUZZER\BUZZER.h"

static uint8_t s_buzzerOn = 0u;

void Buzzer_Init(void)
{
	__HAL_RCC_GPIOB_CLK_ENABLE();

	GPIO_InitTypeDef GPIO_InitStruct = {0};
	GPIO_InitStruct.Pin = BUZZER_PIN;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(BUZZER_PORT, &GPIO_InitStruct);

	Buzzer_Off();
}

void Buzzer_On(void)
{
	HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_RESET);
	s_buzzerOn = 1u;
}

void Buzzer_Off(void)
{
	HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_SET);
	s_buzzerOn = 0u;
}

void Buzzer_Toggle(void)
{
	if (s_buzzerOn != 0u)
	{
		Buzzer_Off();
	}
	else
	{
		Buzzer_On();
	}
}

void Buzzer_Beep(uint16_t ms)
{
	Buzzer_On();
	HAL_Delay(ms);
	Buzzer_Off();
}
