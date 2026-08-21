#include ".\Hardware\HC_SR04\HC_SR04.h"

/**
 * @file HC_SR04.c
 * @brief HC-SR04 超声波测距模块驱动实现。
 */

static uint8_t hc_sr04_inited = 0U;

/**
 * @brief 根据端口使能 GPIO 时钟。
 */
static void HC_SR04_GPIO_Clock_Enable(GPIO_TypeDef *port)
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
 * @brief Trig 引脚输出电平。
 */
static void HC_SR04_Trig_Write(uint8_t state)
{
	HAL_GPIO_WritePin(HC_SR04_Trig_PORT, HC_SR04_Trig_PIN, state ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/**
 * @brief 读取 Echo 引脚电平。
 */
static uint8_t HC_SR04_Echo_Read(void)
{
	return (uint8_t)HAL_GPIO_ReadPin(HC_SR04_Echo_PORT, HC_SR04_Echo_PIN);
}

void HC_SR04_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	HC_SR04_GPIO_Clock_Enable(HC_SR04_Trig_PORT);
	HC_SR04_GPIO_Clock_Enable(HC_SR04_Echo_PORT);

#if (HC_SR04_DISABLE_JTAG_IF_NEEDED)
	__HAL_RCC_AFIO_CLK_ENABLE();
	if (((HC_SR04_Trig_PORT == GPIOA) && ((HC_SR04_Trig_PIN & GPIO_PIN_15) != 0U)) ||
		((HC_SR04_Echo_PORT == GPIOA) && ((HC_SR04_Echo_PIN & GPIO_PIN_15) != 0U)) ||
		((HC_SR04_Trig_PORT == GPIOB) && ((HC_SR04_Trig_PIN & GPIO_PIN_3) != 0U)) ||
		((HC_SR04_Echo_PORT == GPIOB) && ((HC_SR04_Echo_PIN & GPIO_PIN_3) != 0U)) ||
		((HC_SR04_Trig_PORT == GPIOB) && ((HC_SR04_Trig_PIN & GPIO_PIN_4) != 0U)) ||
		((HC_SR04_Echo_PORT == GPIOB) && ((HC_SR04_Echo_PIN & GPIO_PIN_4) != 0U)))
	{
		/* 释放 JTAG 相关引脚，保留 SWD 调试接口。 */
		__HAL_AFIO_REMAP_SWJ_NOJTAG();
	}
#endif

	/* Trig 推挽输出 */
	GPIO_InitStruct.Pin = HC_SR04_Trig_PIN;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
	HAL_GPIO_Init(HC_SR04_Trig_PORT, &GPIO_InitStruct);

	/* Echo 输入 */
	GPIO_InitStruct.Pin = HC_SR04_Echo_PIN;
	GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
	HAL_GPIO_Init(HC_SR04_Echo_PORT, &GPIO_InitStruct);

	HC_SR04_Trig_Write(0U);
	hc_sr04_inited = 1U;
}

HC_SR04_Status_t HC_SR04_ReadEchoTimeUs(uint32_t *echo_time_us)
{
	uint32_t timeout;
	uint32_t pulse_us = 0U;

	if (echo_time_us == NULL)
	{
		return HC_SR04_ERR_PARAM;
	}

	if (!hc_sr04_inited)
	{
		return HC_SR04_ERR_NOT_INIT;
	}

	/* 发送 >=10us Trig 脉冲 */
	HC_SR04_Trig_Write(0U);
	delay_us(2U);
	HC_SR04_Trig_Write(1U);
	delay_us(12U);
	HC_SR04_Trig_Write(0U);

	/* 等待 Echo 拉高 */
	timeout = HC_SR04_ECHO_TIMEOUT_US;
	while ((HC_SR04_Echo_Read() == (uint8_t)GPIO_PIN_RESET) && (timeout > 0U))
	{
		delay_us(1U);
		timeout--;
	}

	if (timeout == 0U)
	{
		return HC_SR04_ERR_TIMEOUT_WAIT_HIGH;
	}

	/* Echo 高电平计时 */
	timeout = HC_SR04_ECHO_TIMEOUT_US;
	while ((HC_SR04_Echo_Read() == (uint8_t)GPIO_PIN_SET) && (timeout > 0U))
	{
		delay_us(1U);
		pulse_us++;
		timeout--;
	}

	if (timeout == 0U)
	{
		return HC_SR04_ERR_TIMEOUT_WAIT_LOW;
	}

	*echo_time_us = pulse_us;
	return HC_SR04_OK;
}

HC_SR04_Status_t HC_SR04_ReadDistanceCm(float *distance_cm)
{
	uint32_t echo_time_us;
	HC_SR04_Status_t ret;

	if (distance_cm == NULL)
	{
		return HC_SR04_ERR_PARAM;
	}

	ret = HC_SR04_ReadEchoTimeUs(&echo_time_us);
	if (ret != HC_SR04_OK)
	{
		return ret;
	}

	/* 距离(cm) = Echo(us) / 58.0 */
	*distance_cm = (float)echo_time_us / 58.0f;
	return HC_SR04_OK;
}

HC_SR04_Status_t HC_SR04_ReadDistanceMm(uint16_t *distance_mm)
{
	uint32_t echo_time_us;
	uint32_t mm;
	HC_SR04_Status_t ret;

	if (distance_mm == NULL)
	{
		return HC_SR04_ERR_PARAM;
	}

	ret = HC_SR04_ReadEchoTimeUs(&echo_time_us);
	if (ret != HC_SR04_OK)
	{
		return ret;
	}

	/* 距离(mm) = Echo(us) * 343 / 2000 */
	mm = (echo_time_us * 343U) / 2000U;
	if (mm > 65535U)
	{
		mm = 65535U;
	}

	*distance_mm = (uint16_t)mm;
	return HC_SR04_OK;
}
