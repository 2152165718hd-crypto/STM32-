#include ".\Hardware\TFT_ST7735\TFT_ST7735.h"

#define ST7735_SWRESET 0x01
#define ST7735_SLPOUT 0x11
#define ST7735_FRMCTR1 0xB1
#define ST7735_FRMCTR2 0xB2
#define ST7735_FRMCTR3 0xB3
#define ST7735_INVCTR 0xB4
#define ST7735_PWCTR1 0xC0
#define ST7735_PWCTR2 0xC1
#define ST7735_PWCTR3 0xC2
#define ST7735_PWCTR4 0xC3
#define ST7735_PWCTR5 0xC4
#define ST7735_VMCTR1 0xC5
#define ST7735_COLMOD 0x3A
#define ST7735_GMCTRP1 0xE0
#define ST7735_GMCTRN1 0xE1
#define ST7735_NORON 0x13
#define ST7735_DISPON 0x29
#define ST7735_CASET 0x2A
#define ST7735_RASET 0x2B
#define ST7735_RAMWR 0x2C
#define ST7735_INVOFF 0x20
#define ST7735_INVON 0x21

#define ST7735_MADCTL 0x36
#define ST7735_MADCTL_MX 0x40
#define ST7735_MADCTL_MY 0x80
#define ST7735_MADCTL_MV 0x20
#define ST7735_MADCTL_ML 0x10

#if (ST7735_USE_SOFT_SPI == 0)
#define ST7735_SPI_TIMEOUT_MS HAL_MAX_DELAY
#endif

static uint16_t st7735_width = ST7735_WIDTH;
static uint16_t st7735_height = ST7735_HEIGHT;
static uint16_t st7735_xstart = ST7735_XSTART;
static uint16_t st7735_ystart = ST7735_YSTART;
static uint8_t st7735_tx_buffer[ST7735_TX_BUFFER_SIZE];
#if (ST7735_USE_SOFT_SPI == 0)
static SPI_HandleTypeDef st7735_hspi;
#if (ST7735_USE_DMA == 1)
static DMA_HandleTypeDef st7735_hdma_tx;
static volatile uint8_t st7735_dma_tx_done = 0U;
static volatile uint8_t st7735_dma_tx_error = 0U;
#endif
#endif

__STATIC_INLINE void ST7735_GPIO_WriteFast(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
	if (state == GPIO_PIN_RESET)
	{
		port->BRR = (uint32_t)pin;
	}
	else
	{
		port->BSRR = (uint32_t)pin;
	}
}

__STATIC_INLINE void ST7735_DC_Command(void)
{
	ST7735_GPIO_WriteFast(ST7735_DC_GPIO_Port, ST7735_DC_Pin, GPIO_PIN_RESET);
}

__STATIC_INLINE void ST7735_DC_Data(void)
{
	ST7735_GPIO_WriteFast(ST7735_DC_GPIO_Port, ST7735_DC_Pin, GPIO_PIN_SET);
}

static void ST7735_EnableGPIOClock(GPIO_TypeDef *port)
{
#if defined(GPIOA)
	if (port == GPIOA)
	{
		__HAL_RCC_GPIOA_CLK_ENABLE();
		return;
	}
#endif
#if defined(GPIOB)
	if (port == GPIOB)
	{
		__HAL_RCC_GPIOB_CLK_ENABLE();
		return;
	}
#endif
#if defined(GPIOC)
	if (port == GPIOC)
	{
		__HAL_RCC_GPIOC_CLK_ENABLE();
		return;
	}
#endif
#if defined(GPIOD)
	if (port == GPIOD)
	{
		__HAL_RCC_GPIOD_CLK_ENABLE();
		return;
	}
#endif
#if defined(GPIOE)
	if (port == GPIOE)
	{
		__HAL_RCC_GPIOE_CLK_ENABLE();
		return;
	}
#endif
}

#if (ST7735_USE_SOFT_SPI == 0)
static void ST7735_EnableSPIClock(SPI_TypeDef *instance)
{
#if defined(SPI1)
	if (instance == SPI1)
	{
		__HAL_RCC_SPI1_CLK_ENABLE();
		return;
	}
#endif
#if defined(SPI2)
	if (instance == SPI2)
	{
		__HAL_RCC_SPI2_CLK_ENABLE();
		return;
	}
#endif
}
#endif

#if (ST7735_USE_SOFT_SPI == 0) && (ST7735_USE_DMA == 1)
static void ST7735_EnableDMAClock(void)
{
	__HAL_RCC_DMA1_CLK_ENABLE();
}

static HAL_StatusTypeDef ST7735_SPI_TransmitDMA(const uint8_t *data, size_t data_size)
{
	while (data_size > 0U)
	{
		uint16_t chunk = (data_size > 0xFFFFU) ? 0xFFFFU : (uint16_t)data_size;
		uint32_t tick_start = HAL_GetTick();

		st7735_dma_tx_done = 0U;
		st7735_dma_tx_error = 0U;

		if (HAL_SPI_Transmit_DMA(&st7735_hspi, (uint8_t *)data, chunk) != HAL_OK)
		{
			return HAL_ERROR;
		}

		while (st7735_dma_tx_done == 0U)
		{
			if ((ST7735_DMA_TIMEOUT_MS != HAL_MAX_DELAY) && ((HAL_GetTick() - tick_start) > ST7735_DMA_TIMEOUT_MS))
			{
				(void)HAL_SPI_DMAStop(&st7735_hspi);
				return HAL_TIMEOUT;
			}
		}

		if (st7735_dma_tx_error != 0U)
		{
			return HAL_ERROR;
		}

		data += chunk;
		data_size -= chunk;
	}

	return HAL_OK;
}
#endif

static void ST7735_InitOutputPin(GPIO_TypeDef *port, uint16_t pin)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	GPIO_InitStruct.Pin = pin;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
	HAL_GPIO_Init(port, &GPIO_InitStruct);
}

#if (ST7735_USE_SOFT_SPI == 0)
static void ST7735_InitAFPin(GPIO_TypeDef *port, uint16_t pin)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	GPIO_InitStruct.Pin = pin;
	GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
	HAL_GPIO_Init(port, &GPIO_InitStruct);
}
#endif

static void ST7735_SPI_Transmit(const uint8_t *data, size_t data_size)
{
#if (ST7735_USE_SOFT_SPI == 0)
#if (ST7735_USE_DMA == 1)
	if (data_size >= ST7735_DMA_MIN_SIZE)
	{
		(void)ST7735_SPI_TransmitDMA(data, data_size);
		return;
	}
#endif

	while (data_size > 0U)
	{
		uint16_t chunk = (data_size > 0xFFFFU) ? 0xFFFFU : (uint16_t)data_size;
		if (HAL_SPI_Transmit(&st7735_hspi, (uint8_t *)data, chunk, ST7735_SPI_TIMEOUT_MS) != HAL_OK)
		{
			break;
		}
		data += chunk;
		data_size -= chunk;
	}
#else
	while (data_size > 0U)
	{
		uint8_t dat = *data++;
		uint8_t i;
		for (i = 0U; i < 8U; i++)
		{
			ST7735_GPIO_WriteFast(ST7735_SCK_GPIO_Port, ST7735_SCK_Pin, GPIO_PIN_RESET);
			ST7735_GPIO_WriteFast(ST7735_MOSI_GPIO_Port, ST7735_MOSI_Pin, (dat & 0x80U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
			ST7735_GPIO_WriteFast(ST7735_SCK_GPIO_Port, ST7735_SCK_Pin, GPIO_PIN_SET);
			dat <<= 1;
		}
		data_size--;
	}
#endif
}

/* ---- CS control ---- */

static void ST7735_Select(void)
{
	ST7735_GPIO_WriteFast(ST7735_CS_GPIO_Port, ST7735_CS_Pin, GPIO_PIN_RESET);
}

static void ST7735_Unselect(void)
{
	ST7735_GPIO_WriteFast(ST7735_CS_GPIO_Port, ST7735_CS_Pin, GPIO_PIN_SET);
}

static void ST7735_Reset(void)
{
	ST7735_GPIO_WriteFast(ST7735_RST_GPIO_Port, ST7735_RST_Pin, GPIO_PIN_RESET);
	HAL_Delay(100);
	ST7735_GPIO_WriteFast(ST7735_RST_GPIO_Port, ST7735_RST_Pin, GPIO_PIN_SET);
	HAL_Delay(100);
}

static void ST7735_WriteCommandRaw(uint8_t cmd)
{
	ST7735_DC_Command();
	ST7735_SPI_Transmit(&cmd, 1U);
}

static void ST7735_WriteCommand(uint8_t cmd)
{
	ST7735_Select();
	ST7735_WriteCommandRaw(cmd);
	ST7735_Unselect();
}

static void ST7735_WriteDataRaw(const uint8_t *data, size_t data_size)
{
	ST7735_SPI_Transmit(data, data_size);
}

static void ST7735_WriteCommandData(uint8_t cmd, const uint8_t *data, size_t data_size)
{
	ST7735_Select();
	ST7735_WriteCommandRaw(cmd);

	if ((data != NULL) && (data_size > 0U))
	{
		ST7735_DC_Data();
		ST7735_WriteDataRaw(data, data_size);
	}

	ST7735_Unselect();
}

static void ST7735_BeginAddressWindowWrite(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
	uint8_t data[4];

	x0 += st7735_xstart;
	y0 += st7735_ystart;
	x1 += st7735_xstart;
	y1 += st7735_ystart;

	ST7735_Select();

	data[0] = (uint8_t)(x0 >> 8);
	data[1] = (uint8_t)(x0 & 0xFFU);
	data[2] = (uint8_t)(x1 >> 8);
	data[3] = (uint8_t)(x1 & 0xFFU);
	ST7735_WriteCommandRaw(ST7735_CASET);
	ST7735_DC_Data();
	ST7735_WriteDataRaw(data, sizeof(data));

	data[0] = (uint8_t)(y0 >> 8);
	data[1] = (uint8_t)(y0 & 0xFFU);
	data[2] = (uint8_t)(y1 >> 8);
	data[3] = (uint8_t)(y1 & 0xFFU);
	ST7735_WriteCommandRaw(ST7735_RASET);
	ST7735_DC_Data();
	ST7735_WriteDataRaw(data, sizeof(data));

	ST7735_WriteCommandRaw(ST7735_RAMWR);
	ST7735_DC_Data();
}

static void ST7735_FillTxBuffer(uint16_t color, size_t byte_count)
{
	size_t i;
	uint8_t color_high = (uint8_t)(color >> 8);
	uint8_t color_low = (uint8_t)(color & 0xFFU);

	for (i = 0U; i < byte_count; i += 2U)
	{
		st7735_tx_buffer[i] = color_high;
		st7735_tx_buffer[i + 1U] = color_low;
	}
}

static void ST7735_WriteColorPixels(uint16_t color, size_t pixel_count)
{
	size_t bytes_remaining = pixel_count * 2U;
	size_t buffer_bytes = (bytes_remaining > ST7735_TX_BUFFER_SIZE) ? ST7735_TX_BUFFER_SIZE : bytes_remaining;

	ST7735_FillTxBuffer(color, buffer_bytes);
	while (bytes_remaining > 0U)
	{
		size_t chunk = (bytes_remaining > buffer_bytes) ? buffer_bytes : bytes_remaining;
		ST7735_WriteDataRaw(st7735_tx_buffer, chunk);
		bytes_remaining -= chunk;
	}
}

static void ST7735_SPIInit(void)
{
#if (ST7735_USE_SOFT_SPI == 0)
	ST7735_EnableSPIClock(ST7735_SPI);

	st7735_hspi.Instance = ST7735_SPI;
	st7735_hspi.Init.Mode = SPI_MODE_MASTER;
	st7735_hspi.Init.Direction = SPI_DIRECTION_1LINE;
	st7735_hspi.Init.DataSize = SPI_DATASIZE_8BIT;
	st7735_hspi.Init.CLKPolarity = SPI_POLARITY_LOW;
	st7735_hspi.Init.CLKPhase = SPI_PHASE_1EDGE;
	st7735_hspi.Init.NSS = SPI_NSS_SOFT;
	st7735_hspi.Init.BaudRatePrescaler = ST7735_SPI_BAUDRATE_PRESCALER;
	st7735_hspi.Init.FirstBit = SPI_FIRSTBIT_MSB;
	st7735_hspi.Init.TIMode = SPI_TIMODE_DISABLE;
	st7735_hspi.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
	st7735_hspi.Init.CRCPolynomial = 7U;

	(void)HAL_SPI_Init(&st7735_hspi);

	#if (ST7735_USE_DMA == 1)
	ST7735_EnableDMAClock();

	st7735_hdma_tx.Instance = ST7735_SPI_TX_DMA_CHANNEL;
	st7735_hdma_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
	st7735_hdma_tx.Init.PeriphInc = DMA_PINC_DISABLE;
	st7735_hdma_tx.Init.MemInc = DMA_MINC_ENABLE;
	st7735_hdma_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
	st7735_hdma_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
	st7735_hdma_tx.Init.Mode = DMA_NORMAL;
	st7735_hdma_tx.Init.Priority = DMA_PRIORITY_HIGH;
	(void)HAL_DMA_Init(&st7735_hdma_tx);

	__HAL_LINKDMA(&st7735_hspi, hdmatx, st7735_hdma_tx);

	HAL_NVIC_SetPriority(ST7735_SPI_TX_DMA_IRQn, 1U, 0U);
	HAL_NVIC_EnableIRQ(ST7735_SPI_TX_DMA_IRQn);
	#endif
#endif
}

static void ST7735_GPIOInit(void)
{
	__HAL_RCC_AFIO_CLK_ENABLE();

	ST7735_EnableGPIOClock(ST7735_RST_GPIO_Port);
	ST7735_EnableGPIOClock(ST7735_DC_GPIO_Port);
	ST7735_EnableGPIOClock(ST7735_CS_GPIO_Port);
	ST7735_EnableGPIOClock(ST7735_BL_GPIO_Port);
	ST7735_EnableGPIOClock(ST7735_SCK_GPIO_Port);
	ST7735_EnableGPIOClock(ST7735_MOSI_GPIO_Port);

	ST7735_InitOutputPin(ST7735_RST_GPIO_Port, ST7735_RST_Pin);
	ST7735_InitOutputPin(ST7735_DC_GPIO_Port, ST7735_DC_Pin);
	ST7735_InitOutputPin(ST7735_CS_GPIO_Port, ST7735_CS_Pin);
	ST7735_InitOutputPin(ST7735_BL_GPIO_Port, ST7735_BL_Pin);

	#if (ST7735_USE_SOFT_SPI == 0)
	ST7735_InitAFPin(ST7735_SCK_GPIO_Port, ST7735_SCK_Pin);
	ST7735_InitAFPin(ST7735_MOSI_GPIO_Port, ST7735_MOSI_Pin);
	#else
	ST7735_InitOutputPin(ST7735_SCK_GPIO_Port, ST7735_SCK_Pin);
	ST7735_InitOutputPin(ST7735_MOSI_GPIO_Port, ST7735_MOSI_Pin);
	#endif

	ST7735_GPIO_WriteFast(ST7735_CS_GPIO_Port, ST7735_CS_Pin, GPIO_PIN_SET);
	ST7735_GPIO_WriteFast(ST7735_DC_GPIO_Port, ST7735_DC_Pin, GPIO_PIN_SET);
	ST7735_GPIO_WriteFast(ST7735_RST_GPIO_Port, ST7735_RST_Pin, GPIO_PIN_SET);
	ST7735_GPIO_WriteFast(ST7735_SCK_GPIO_Port, ST7735_SCK_Pin, GPIO_PIN_SET);
	ST7735_GPIO_WriteFast(ST7735_MOSI_GPIO_Port, ST7735_MOSI_Pin, GPIO_PIN_SET);
	ST7735_Backlight(0U);
}

void ST7735_Backlight(uint8_t on)
{
	GPIO_PinState off_level = (ST7735_BL_ACTIVE_LEVEL == GPIO_PIN_SET) ? GPIO_PIN_RESET : GPIO_PIN_SET;
	ST7735_GPIO_WriteFast(ST7735_BL_GPIO_Port, ST7735_BL_Pin, on ? ST7735_BL_ACTIVE_LEVEL : off_level);
}

void ST7735_SetRotation(uint8_t rotation)
{
	uint8_t madctl;

	rotation &= 0x03U;
	switch (rotation)
	{
	case ST7735_ROTATION_0:
		madctl = ST7735_MADCTL_MODE;
		st7735_width = ST7735_WIDTH;
		st7735_height = ST7735_HEIGHT;
		st7735_xstart = 2U;
		st7735_ystart = 1U;
		break;
	case ST7735_ROTATION_90:
		madctl = (uint8_t)(ST7735_MADCTL_MX | ST7735_MADCTL_MV | ST7735_MADCTL_ML | ST7735_MADCTL_MODE);
		st7735_width = ST7735_HEIGHT;
		st7735_height = ST7735_WIDTH;
		st7735_xstart = 1U;
		st7735_ystart = 2U;
		break;
	case ST7735_ROTATION_180:
		madctl = (uint8_t)(ST7735_MADCTL_MX | ST7735_MADCTL_MY | ST7735_MADCTL_MODE);
		st7735_width = ST7735_WIDTH;
		st7735_height = ST7735_HEIGHT;
		st7735_xstart = 2U;
		st7735_ystart = 1U;
		break;
	case ST7735_ROTATION_270:
	default:
		madctl = (uint8_t)(ST7735_MADCTL_MY | ST7735_MADCTL_MV | ST7735_MADCTL_MODE);
		st7735_width = ST7735_HEIGHT;
		st7735_height = ST7735_WIDTH;
		st7735_xstart = 1U;
		st7735_ystart = 2U;
		break;
	}

	if (ST7735_FLIP_X)
	{
		madctl ^= ST7735_MADCTL_MX;
	}
	if (ST7735_FLIP_Y)
	{
		madctl ^= ST7735_MADCTL_MY;
	}

	ST7735_WriteCommandData(ST7735_MADCTL, &madctl, sizeof(madctl));
}

void ST7735_Init(void)
{
	const uint8_t frmctr1[] = {0x05U, 0x3CU, 0x3CU};
	const uint8_t frmctr3[] = {0x05U, 0x3CU, 0x3CU, 0x05U, 0x3CU, 0x3CU};
	const uint8_t invctr = 0x03U;
	const uint8_t pwctr1[] = {0x28U, 0x08U, 0x04U};
	const uint8_t pwctr2 = 0xC0U;
	const uint8_t pwctr3[] = {0x0DU, 0x00U};
	const uint8_t pwctr4[] = {0x8DU, 0x2AU};
	const uint8_t pwctr5[] = {0x8DU, 0xEEU};
	const uint8_t vmctr1 = 0x1AU;
	const uint8_t gmctrp1[] = {0x04U, 0x22U, 0x07U, 0x0AU, 0x2EU, 0x30U, 0x25U, 0x2AU,
							0x28U, 0x26U, 0x2EU, 0x3AU, 0x00U, 0x01U, 0x03U, 0x13U};
	const uint8_t gmctrn1[] = {0x04U, 0x16U, 0x06U, 0x0DU, 0x2DU, 0x26U, 0x23U, 0x27U,
							0x27U, 0x25U, 0x2DU, 0x3BU, 0x00U, 0x01U, 0x04U, 0x13U};
	const uint8_t colmod = 0x05U;

	ST7735_GPIOInit();
	ST7735_SPIInit();

	ST7735_Reset();
	ST7735_Backlight(1U);
	HAL_Delay(100);

	ST7735_WriteCommand(ST7735_SLPOUT);
	HAL_Delay(120);
	ST7735_WriteCommandData(ST7735_FRMCTR1, frmctr1, sizeof(frmctr1));
	ST7735_WriteCommandData(ST7735_FRMCTR2, frmctr1, sizeof(frmctr1));
	ST7735_WriteCommandData(ST7735_FRMCTR3, frmctr3, sizeof(frmctr3));
	ST7735_WriteCommandData(ST7735_INVCTR, &invctr, sizeof(invctr));
	ST7735_WriteCommandData(ST7735_PWCTR1, pwctr1, sizeof(pwctr1));
	ST7735_WriteCommandData(ST7735_PWCTR2, &pwctr2, sizeof(pwctr2));
	ST7735_WriteCommandData(ST7735_PWCTR3, pwctr3, sizeof(pwctr3));
	ST7735_WriteCommandData(ST7735_PWCTR4, pwctr4, sizeof(pwctr4));
	ST7735_WriteCommandData(ST7735_PWCTR5, pwctr5, sizeof(pwctr5));
	ST7735_WriteCommandData(ST7735_VMCTR1, &vmctr1, sizeof(vmctr1));
	ST7735_SetRotation(ST7735_ROTATION);

	ST7735_WriteCommandData(ST7735_GMCTRP1, gmctrp1, sizeof(gmctrp1));
	ST7735_WriteCommandData(ST7735_GMCTRN1, gmctrn1, sizeof(gmctrn1));
	ST7735_WriteCommandData(ST7735_COLMOD, &colmod, sizeof(colmod));

	if (ST7735_INVERSE)
	{
		ST7735_WriteCommand(ST7735_INVON);
	}
	else
	{
		ST7735_WriteCommand(ST7735_INVOFF);
	}

	ST7735_WriteCommand(ST7735_NORON);
	HAL_Delay(10);
	ST7735_WriteCommand(ST7735_DISPON);
	HAL_Delay(120);

	ST7735_FillScreen(ST7735_BLUE);
}

#if (ST7735_USE_SOFT_SPI == 0) && (ST7735_USE_DMA == 1)
void ST7735_SPI_DMA_IRQHandler(void)
{
	HAL_DMA_IRQHandler(&st7735_hdma_tx);
}

void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
	if (hspi == &st7735_hspi)
	{
		st7735_dma_tx_done = 1U;
	}
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
	if (hspi == &st7735_hspi)
	{
		st7735_dma_tx_error = 1U;
		st7735_dma_tx_done = 1U;
	}
}
#else
void ST7735_SPI_DMA_IRQHandler(void)
{
}
#endif

static uint16_t ST7735_ClipDimension(uint16_t start, uint16_t length, uint16_t limit)
{
	uint32_t end;

	if ((start >= limit) || (length == 0U))
	{
		return 0U;
	}

	end = (uint32_t)start + (uint32_t)length;
	if (end > limit)
	{
		return (uint16_t)(limit - start);
	}

	return length;
}

void ST7735_DrawPixel(uint16_t x, uint16_t y, uint16_t color)
{
	uint8_t data[2];

	if ((x >= st7735_width) || (y >= st7735_height))
	{
		return;
	}

	ST7735_BeginAddressWindowWrite(x, y, x, y);

	data[0] = (uint8_t)(color >> 8);
	data[1] = (uint8_t)(color & 0xFFU);

	ST7735_WriteDataRaw(data, sizeof(data));
	ST7735_Unselect();
}

void ST7735_DrawRectangle(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color)
{
	uint16_t clipped_width;
	uint16_t clipped_height;
	size_t pixel_count;

	clipped_width = ST7735_ClipDimension(x, width, st7735_width);
	clipped_height = ST7735_ClipDimension(y, height, st7735_height);
	if ((clipped_width == 0U) || (clipped_height == 0U))
	{
		return;
	}

	pixel_count = (size_t)clipped_width * (size_t)clipped_height;
	ST7735_BeginAddressWindowWrite(x, y, x + clipped_width - 1U, y + clipped_height - 1U);
	ST7735_WriteColorPixels(color, pixel_count);
	ST7735_Unselect();
}

static uint8_t ST7735_GetCharIndex(char c, const FontDef *font, uint16_t *index)
{
	uint8_t code = (uint8_t)c;

	if (font == &Font_Custom)
	{
		if (code < 46U)
		{
			return 0U;
		}
		*index = (uint16_t)(code - 46U);
		return 1U;
	}

	if ((code < 32U) || (code > 126U))
	{
		return 0U;
	}

	*index = (uint16_t)(code - 32U);
	return 1U;
}

void ST7735_DrawChar(uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bgColor, const FontDef *font)
{
	uint32_t i;
	uint32_t j;
	uint32_t b;
	uint16_t pixel_color;
	uint16_t char_index;
	uint32_t mask;
	size_t buffer_used = 0U;

	if ((font == NULL) || (font->data == NULL) || (font->width == 0U) || (font->height == 0U))
	{
		return;
	}

	if ((ST7735_ClipDimension(x, font->width, st7735_width) != font->width) ||
		(ST7735_ClipDimension(y, font->height, st7735_height) != font->height))
	{
		return;
	}

	if (!ST7735_GetCharIndex(c, font, &char_index))
	{
		if (font == &Font_Custom)
		{
			return;
		}
		char_index = (uint16_t)('?' - 32);
	}

	mask = (font->width > 16U) ? 0x80000000UL : 0x8000UL;
	ST7735_BeginAddressWindowWrite(x, y, x + font->width - 1U, y + font->height - 1U);

	for (i = 0U; i < font->height; i++)
	{
		b = font->data[char_index * font->height + i];
		for (j = 0U; j < font->width; j++)
		{
			if (buffer_used > (ST7735_TX_BUFFER_SIZE - 2U))
			{
				ST7735_WriteDataRaw(st7735_tx_buffer, buffer_used);
				buffer_used = 0U;
			}

			pixel_color = ((b << j) & mask) ? color : bgColor;
			st7735_tx_buffer[buffer_used] = (uint8_t)(pixel_color >> 8);
			st7735_tx_buffer[buffer_used + 1U] = (uint8_t)(pixel_color & 0xFFU);
			buffer_used += 2U;
		}
	}

	if (buffer_used > 0U)
	{
		ST7735_WriteDataRaw(st7735_tx_buffer, buffer_used);
	}
	ST7735_Unselect();
}

void ST7735_DrawString(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bgColor, const FontDef *font)
{
	if ((str == NULL) || (font == NULL))
	{
		return;
	}

	while (*str != '\0')
	{
		if (*str == '\n')
		{
			x = 0U;
			y += font->height;
			str++;
			continue;
		}

		if (*str == '\r')
		{
			str++;
			continue;
		}

		if (x + font->width > st7735_width)
		{
			x = 0U;
			y += font->height;
		}

		if (y + font->height > st7735_height)
		{
			break;
		}

		ST7735_DrawChar(x, y, *str, color, bgColor, font);
		x += font->width;
		str++;
	}
}

void ST7735_FillScreen(uint16_t color)
{
	ST7735_DrawRectangle(0U, 0U, st7735_width, st7735_height, color);
}

void ST7735_DrawImage(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const uint8_t *image)
{
	uint16_t row;
	uint16_t clipped_width;
	uint16_t clipped_height;
	const uint8_t *row_data;

	if (image == NULL)
	{
		return;
	}

	clipped_width = ST7735_ClipDimension(x, width, st7735_width);
	clipped_height = ST7735_ClipDimension(y, height, st7735_height);
	if ((clipped_width == 0U) || (clipped_height == 0U))
	{
		return;
	}

	ST7735_BeginAddressWindowWrite(x, y, x + clipped_width - 1U, y + clipped_height - 1U);

	if (clipped_width == width)
	{
		ST7735_WriteDataRaw(image, (size_t)clipped_width * clipped_height * 2U);
	}
	else
	{
		for (row = 0U; row < clipped_height; row++)
		{
			row_data = image + ((size_t)row * width * 2U);
			ST7735_WriteDataRaw(row_data, (size_t)clipped_width * 2U);
		}
	}
	ST7735_Unselect();
}
