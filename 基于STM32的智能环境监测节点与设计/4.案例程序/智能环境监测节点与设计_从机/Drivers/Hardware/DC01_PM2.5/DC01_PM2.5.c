#include ".\Hardware\DC01_PM2.5\DC01_PM2.5.h"

static UART_HandleTypeDef s_uart_handle;
static volatile uint8_t s_frame_buffer[DC01_PM2_5_FRAME_LEN];
static volatile uint8_t s_frame_index = 0U;
static volatile uint16_t s_raw_concentration_ug_m3 = 0U;
static volatile uint8_t s_data_valid = 0U;
static float s_k_factor = DC01_PM2_5_DEFAULT_K_FACTOR;

static void DC01_PM2_5_EnableRemap(void)
{
	__HAL_RCC_AFIO_CLK_ENABLE();

#if (DC01_PM2_5_UART_REMAP_MODE == 1U)
	__HAL_AFIO_REMAP_USART3_PARTIAL();
#elif (DC01_PM2_5_UART_REMAP_MODE == 2U)
	__HAL_AFIO_REMAP_USART3_ENABLE();
#else
	__HAL_AFIO_REMAP_USART3_DISABLE();
#endif
}

static void DC01_PM2_5_GPIO_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	DC01_PM2_5_UART_TX_CLK_ENABLE();
	DC01_PM2_5_UART_RX_CLK_ENABLE();

	GPIO_InitStruct.Pin = DC01_PM2_5_UART_TX_PIN;
	GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
	HAL_GPIO_Init(DC01_PM2_5_UART_TX_GPIO_PORT, &GPIO_InitStruct);

	GPIO_InitStruct.Pin = DC01_PM2_5_UART_RX_PIN;
	GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	GPIO_InitStruct.Pull = GPIO_PULLUP;
	HAL_GPIO_Init(DC01_PM2_5_UART_RX_GPIO_PORT, &GPIO_InitStruct);
}

static void DC01_PM2_5_UART_Init(void)
{
	DC01_PM2_5_UART_CLK_ENABLE();

	s_uart_handle.Instance = DC01_PM2_5_UART;
	s_uart_handle.Init.BaudRate = DC01_PM2_5_UART_BAUDRATE;
	s_uart_handle.Init.WordLength = UART_WORDLENGTH_8B;
	s_uart_handle.Init.StopBits = UART_STOPBITS_1;
	s_uart_handle.Init.Parity = UART_PARITY_NONE;
	s_uart_handle.Init.Mode = UART_MODE_TX_RX;
	s_uart_handle.Init.HwFlowCtl = UART_HWCONTROL_NONE;
	s_uart_handle.Init.OverSampling = UART_OVERSAMPLING_16;
	HAL_UART_Init(&s_uart_handle);

	__HAL_UART_FLUSH_DRREGISTER(&s_uart_handle);
	__HAL_UART_ENABLE_IT(&s_uart_handle, UART_IT_RXNE);
	__HAL_UART_ENABLE_IT(&s_uart_handle, UART_IT_ERR);

	HAL_NVIC_SetPriority(DC01_PM2_5_UART_IRQn, 2U, 0U);
	HAL_NVIC_EnableIRQ(DC01_PM2_5_UART_IRQn);
}

static uint8_t DC01_PM2_5_GetChecksum(const volatile uint8_t *frame)
{
	return (uint8_t)((frame[0] + frame[1] + frame[2]) & DC01_PM2_5_DATA_MASK);
}

static void DC01_PM2_5_ParseByte(uint8_t byte)
{
	if (s_frame_index == 0U)
	{
		if (byte == DC01_PM2_5_FRAME_HEADER)
		{
			s_frame_buffer[0] = byte;
			s_frame_index = 1U;
		}
		return;
	}

	s_frame_buffer[s_frame_index] = byte;
	s_frame_index++;

	if (s_frame_index >= DC01_PM2_5_FRAME_LEN)
	{
		uint8_t checksum = DC01_PM2_5_GetChecksum(s_frame_buffer);
		uint8_t frame_checksum = (uint8_t)(s_frame_buffer[3] & DC01_PM2_5_DATA_MASK);

		if (checksum == frame_checksum)
		{
			uint16_t concentration = 0U;

			concentration = (uint16_t)((uint16_t)(s_frame_buffer[1] & DC01_PM2_5_DATA_MASK) << 7);
			concentration |= (uint16_t)(s_frame_buffer[2] & DC01_PM2_5_DATA_MASK);

			s_raw_concentration_ug_m3 = concentration;
			s_data_valid = 1U;
		}

		if (byte == DC01_PM2_5_FRAME_HEADER)
		{
			s_frame_buffer[0] = byte;
			s_frame_index = 1U;
		}
		else
		{
			s_frame_index = 0U;
		}
	}
}

void DC01_PM2_5_Init(void)
{
	s_frame_index = 0U;
	s_data_valid = 0U;

	DC01_PM2_5_EnableRemap();
	DC01_PM2_5_GPIO_Init();
	DC01_PM2_5_UART_Init();
}

uint8_t DC01_PM2_5_IsDataValid(void)
{
	return s_data_valid;
}

uint16_t DC01_PM2_5_GetRawConcentration(void)
{
	return s_raw_concentration_ug_m3;
}

float DC01_PM2_5_GetCalibratedConcentration(void)
{
	return ((float)s_raw_concentration_ug_m3) * s_k_factor;
}

void DC01_PM2_5_SetKFactor(float k_factor)
{
	if (k_factor > 0.0f)
	{
		s_k_factor = k_factor;
	}
}

float DC01_PM2_5_GetKFactor(void)
{
	return s_k_factor;
}

uint8_t DC01_PM2_5_Read(DC01_PM2_5_Data_t *data)
{
	if (data == NULL)
	{
		return 0U;
	}

	data->raw_ug_m3 = s_raw_concentration_ug_m3;
	data->calibrated_ug_m3 = ((float)s_raw_concentration_ug_m3) * s_k_factor;
	data->valid = s_data_valid;

	return s_data_valid;
}

void USART3_IRQHandler(void)
{
	if ((__HAL_UART_GET_IT_SOURCE(&s_uart_handle, UART_IT_RXNE) != RESET) &&
		(__HAL_UART_GET_FLAG(&s_uart_handle, UART_FLAG_RXNE) != RESET))
	{
		uint8_t data = (uint8_t)(s_uart_handle.Instance->DR & 0x00FFU);
		DC01_PM2_5_ParseByte(data);
	}

	if ((__HAL_UART_GET_FLAG(&s_uart_handle, UART_FLAG_ORE) != RESET) ||
		(__HAL_UART_GET_FLAG(&s_uart_handle, UART_FLAG_NE) != RESET) ||
		(__HAL_UART_GET_FLAG(&s_uart_handle, UART_FLAG_FE) != RESET) ||
		(__HAL_UART_GET_FLAG(&s_uart_handle, UART_FLAG_PE) != RESET))
	{
		volatile uint32_t tmpreg = 0U;
		tmpreg = s_uart_handle.Instance->SR;
		tmpreg = s_uart_handle.Instance->DR;
		(void)tmpreg;

		s_frame_index = 0U;
	}
}