#include "DC01.h"
#include "stm32f1xx.h"

#define DC01_FRAME_HEAD   0xA5U
#define DC01_FRAME_LEN    4U

static volatile uint8_t dc01_new_data = 0U;
static volatile DC01_Data_t dc01_latest = {0U, 0U, 0U, 0U};
static uint8_t dc01_frame[DC01_FRAME_LEN];
static uint8_t dc01_idx = 0U;

static uint32_t DC01_GetPCLK1Hz(void)
{
	uint32_t ppre1 = (RCC->CFGR >> 8U) & 0x7U;
	uint32_t apb1_div = 1U;

	if (ppre1 >= 4U)
	{
		/* APB prescaler encoding: 100->2, 101->4, 110->8, 111->16 */
		apb1_div = 1U << (ppre1 - 3U);
	}

	return SystemCoreClock / apb1_div;
}

static void DC01_GPIO_Config(void)
{
	RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;

	/* PA2 -> USART2_TX: Alternate function push-pull, 50MHz */
	GPIOA->CRL &= ~(GPIO_CRL_MODE2 | GPIO_CRL_CNF2);
	GPIOA->CRL |= GPIO_CRL_MODE2 | GPIO_CRL_CNF2_1;

	/* PA3 -> USART2_RX: Floating input */
	GPIOA->CRL &= ~(GPIO_CRL_MODE3 | GPIO_CRL_CNF3);
	GPIOA->CRL |= GPIO_CRL_CNF3_0;
}

void DC01_Init(uint32_t baudrate)
{
	uint32_t pclk1;
	uint32_t usartdiv;

	if (baudrate == 0U)
	{
		baudrate = 9600U;
	}

	DC01_GPIO_Config();
	RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
	RCC->APB1RSTR |= RCC_APB1RSTR_USART2RST;
	RCC->APB1RSTR &= ~RCC_APB1RSTR_USART2RST;

	/* BRR = PCLK1 / baud (fractional encoding uses BRR low 4 bits) */
	pclk1 = DC01_GetPCLK1Hz();
	usartdiv = (pclk1 + (baudrate / 2U)) / baudrate;
	USART2->BRR = usartdiv;

	USART2->CR2 = 0U;
	USART2->CR3 = 0U;
	USART2->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;
	USART2->CR1 |= USART_CR1_UE;

	/* Clear any stale RX/error flags before enabling interrupt handling. */
	(void)USART2->SR;
	(void)USART2->DR;

	NVIC_SetPriority(USART2_IRQn, 2U);
	NVIC_EnableIRQ(USART2_IRQn);
}

uint8_t DC01_HasNewData(void)
{
	return dc01_new_data;
}

uint8_t DC01_ReadDataRaw(DC01_Data_t *data)
{
	uint32_t primask;

	if ((data == 0) || (dc01_new_data == 0U))
	{
		return 0U;
	}

	primask = __get_PRIMASK();
	__disable_irq();

	if (dc01_new_data == 0U)
	{
		if (primask == 0U)
		{
			__enable_irq();
		}
		return 0U;
	}

	data->pm25_x10 = dc01_latest.pm25_x10;
	data->pm10_x10 = dc01_latest.pm10_x10;
	data->id_low = dc01_latest.id_low;
	data->id_high = dc01_latest.id_high;

	dc01_new_data = 0U;

	if (primask == 0U)
	{
		__enable_irq();
	}

	return 1U;
}

uint8_t DC01_ReadPMugm3(uint16_t *pm25, uint16_t *pm10)
{
	DC01_Data_t data;

	if ((pm25 == 0) || (pm10 == 0))
	{
		return 0U;
	}

	if (DC01_ReadDataRaw(&data) == 0U)
	{
		return 0U;
	}

	/* Sensor output unit is 0.1 ug/m3, convert to ug/m3 by rounded division */
	*pm25 = (uint16_t)((data.pm25_x10 + 5U) / 10U);
	*pm10 = (uint16_t)((data.pm10_x10 + 5U) / 10U);

	return 1U;
}

void USART2_IRQHandler(void)
{
	if ((USART2->SR & (USART_SR_ORE | USART_SR_NE | USART_SR_FE | USART_SR_PE)) != 0U)
	{
		volatile uint8_t dummy = (uint8_t)USART2->DR;
		(void)dummy;
		dc01_idx = 0U;
		return;
	}

	if ((USART2->SR & USART_SR_RXNE) != 0U)
	{
		uint8_t rx = (uint8_t)USART2->DR;

		switch (dc01_idx)
		{
			case 0U:
				if (rx == DC01_FRAME_HEAD)
				{
					dc01_frame[dc01_idx++] = rx;
				}
				break;

			default:
				/* If a new frame head appears mid-frame, resync immediately. */
				if (rx == DC01_FRAME_HEAD)
				{
					dc01_frame[0] = rx;
					dc01_idx = 1U;
					break;
				}

				dc01_frame[dc01_idx++] = rx;
				if (dc01_idx >= DC01_FRAME_LEN)
				{
					/* SUM is low 7 bits of previous bytes sum. */
					uint8_t checksum = (uint8_t)((dc01_frame[0] + dc01_frame[1] + dc01_frame[2]) & 0x7FU);
					if (dc01_frame[3] == checksum)
					{
						uint16_t conc_x10 = (uint16_t)((((uint16_t)dc01_frame[1] & 0x7FU) << 7U) | ((uint16_t)dc01_frame[2] & 0x7FU));

						/* Sensor UART value is 0.1 ug/m3. Keep existing API: mirror to PM2.5/PM10. */
						dc01_latest.pm25_x10 = conc_x10;
						dc01_latest.pm10_x10 = conc_x10;
						dc01_latest.id_low = 0U;
						dc01_latest.id_high = 0U;
						dc01_new_data = 1U;
					}
					dc01_idx = 0U;
				}
				break;
		}
	}
}
