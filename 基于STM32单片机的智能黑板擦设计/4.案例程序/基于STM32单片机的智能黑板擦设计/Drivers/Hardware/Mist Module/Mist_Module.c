#include "Mist_Module.h"

void Mist_Module_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure = {0};

	__HAL_RCC_GPIOA_CLK_ENABLE();

	GPIO_InitStructure.Pin = MIST_GPIO_PIN;
	GPIO_InitStructure.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStructure.Pull = GPIO_NOPULL;
	GPIO_InitStructure.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(MIST_GPIO_PORT, &GPIO_InitStructure);

	/* PA5高电平导通，低电平关闭，上电默认关闭水雾 */
	Mist_Module_Off();
}

void Mist_Module_On(void)
{
	HAL_GPIO_WritePin(MIST_GPIO_PORT, MIST_GPIO_PIN, GPIO_PIN_SET);
}

void Mist_Module_Off(void)
{
	HAL_GPIO_WritePin(MIST_GPIO_PORT, MIST_GPIO_PIN, GPIO_PIN_RESET);
}

void Mist_Module_Toggle(void)
{
	if (HAL_GPIO_ReadPin(MIST_GPIO_PORT, MIST_GPIO_PIN) == GPIO_PIN_SET)
	{
		Mist_Module_Off();
	}
	else
	{
		Mist_Module_On();
	}
}
