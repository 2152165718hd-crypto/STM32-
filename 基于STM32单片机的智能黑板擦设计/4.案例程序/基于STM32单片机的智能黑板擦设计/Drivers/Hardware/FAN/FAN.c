#include "FAN.h"

void FAN_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure = {0};

	__HAL_RCC_GPIOA_CLK_ENABLE();

	GPIO_InitStructure.Pin = FAN_GPIO_PIN;
	GPIO_InitStructure.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStructure.Pull = GPIO_NOPULL;
	GPIO_InitStructure.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(FAN_GPIO_PORT, &GPIO_InitStructure);

	/* PA4低电平导通，高电平关闭，上电默认关闭风扇 */
	FAN_Off();
}

void FAN_On(void)
{
	HAL_GPIO_WritePin(FAN_GPIO_PORT, FAN_GPIO_PIN, GPIO_PIN_RESET);
}

void FAN_Off(void)
{
	HAL_GPIO_WritePin(FAN_GPIO_PORT, FAN_GPIO_PIN, GPIO_PIN_SET);
}

void FAN_Toggle(void)
{
	if (HAL_GPIO_ReadPin(FAN_GPIO_PORT, FAN_GPIO_PIN) == GPIO_PIN_SET)
	{
		FAN_On();
	}
	else
	{
		FAN_Off();
	}
}
