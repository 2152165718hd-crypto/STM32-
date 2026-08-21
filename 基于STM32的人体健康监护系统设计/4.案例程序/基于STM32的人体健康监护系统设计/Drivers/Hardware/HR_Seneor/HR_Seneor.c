#include ".\Hardware\HR_Seneor\HR_Seneor.h"

#define HR_SENSOR_ACK 0U
#define HR_SENSOR_NACK 1U
#define HR_SENSOR_I2C_TIMEOUT_MS 100U
#define HR_SENSOR_FIFO_BYTES_PER_SAMPLE 6U

#if (HR_SENSOR_USE_HARDWARE_I2C == 1U)
static I2C_HandleTypeDef s_hi2c2;
#else
static uint8_t s_dwt_ready = 0U;
#define HR_SENSOR_SOFT_I2C_DELAY_US 3U
#define HR_SENSOR_SOFT_I2C_RETRY 3U
#endif

static void HR_Sensor_EnableGPIOClock(GPIO_TypeDef *port)
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

#if (HR_SENSOR_USE_HARDWARE_I2C == 1U)
static void HR_Sensor_I2C_Init(void)
{
	GPIO_InitTypeDef gpio_init = {0};

	HR_Sensor_EnableGPIOClock(HR_SENSOR_SCL_PORT);
	if (HR_SENSOR_SDA_PORT != HR_SENSOR_SCL_PORT)
	{
		HR_Sensor_EnableGPIOClock(HR_SENSOR_SDA_PORT);
	}

	__HAL_RCC_AFIO_CLK_ENABLE();
	__HAL_RCC_I2C2_CLK_ENABLE();

	gpio_init.Mode = GPIO_MODE_AF_OD;
	gpio_init.Pull = GPIO_PULLUP;
	gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;

	if (HR_SENSOR_SCL_PORT == HR_SENSOR_SDA_PORT)
	{
		gpio_init.Pin = (uint16_t)(HR_SENSOR_SCL_PIN | HR_SENSOR_SDA_PIN);
		HAL_GPIO_Init(HR_SENSOR_SCL_PORT, &gpio_init);
	}
	else
	{
		gpio_init.Pin = HR_SENSOR_SCL_PIN;
		HAL_GPIO_Init(HR_SENSOR_SCL_PORT, &gpio_init);

		gpio_init.Pin = HR_SENSOR_SDA_PIN;
		HAL_GPIO_Init(HR_SENSOR_SDA_PORT, &gpio_init);
	}

	s_hi2c2.Instance = I2C2;
	s_hi2c2.Init.ClockSpeed = 100000U;
	s_hi2c2.Init.DutyCycle = I2C_DUTYCYCLE_2;
	s_hi2c2.Init.OwnAddress1 = 0U;
	s_hi2c2.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
	s_hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
	s_hi2c2.Init.OwnAddress2 = 0U;
	s_hi2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
	s_hi2c2.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

	(void)HAL_I2C_Init(&s_hi2c2);
}

static HAL_StatusTypeDef HR_Sensor_WriteRegInternal(uint8_t reg, uint8_t value)
{
	return HAL_I2C_Mem_Write(&s_hi2c2,
							 (uint16_t)(HR_SENSOR_MAX30102_ADDR << 1),
							 reg,
							 I2C_MEMADD_SIZE_8BIT,
							 &value,
							 1U,
							 HR_SENSOR_I2C_TIMEOUT_MS);
}

static HAL_StatusTypeDef HR_Sensor_ReadRegInternal(uint8_t reg, uint8_t *value)
{
	if (value == NULL)
	{
		return HAL_ERROR;
	}

	return HAL_I2C_Mem_Read(&s_hi2c2,
							(uint16_t)(HR_SENSOR_MAX30102_ADDR << 1),
							reg,
							I2C_MEMADD_SIZE_8BIT,
							value,
							1U,
							HR_SENSOR_I2C_TIMEOUT_MS);
}

static HAL_StatusTypeDef HR_Sensor_ReadBurstInternal(uint8_t reg, uint8_t *buffer, uint16_t length)
{
	if ((buffer == NULL) || (length == 0U))
	{
		return HAL_ERROR;
	}

	return HAL_I2C_Mem_Read(&s_hi2c2,
							(uint16_t)(HR_SENSOR_MAX30102_ADDR << 1),
							reg,
							I2C_MEMADD_SIZE_8BIT,
							buffer,
							length,
							HR_SENSOR_I2C_TIMEOUT_MS);
}
#else
static void HR_Sensor_EnableDwtDelay(void)
{
	if (s_dwt_ready != 0U)
	{
		return;
	}

	CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
	DWT->CYCCNT = 0U;
	DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
	s_dwt_ready = 1U;
}

static void HR_Sensor_DelayUs(uint32_t us)
{
	uint32_t cycles_per_us = HAL_RCC_GetHCLKFreq() / 1000000U;
	uint32_t wait_cycles = cycles_per_us * us;
	uint32_t start = DWT->CYCCNT;

	while ((uint32_t)(DWT->CYCCNT - start) < wait_cycles)
	{
	}
}

static void HR_Sensor_SCL_High(void)
{
	HAL_GPIO_WritePin(HR_SENSOR_SCL_PORT, HR_SENSOR_SCL_PIN, GPIO_PIN_SET);
}

static void HR_Sensor_SCL_Low(void)
{
	HAL_GPIO_WritePin(HR_SENSOR_SCL_PORT, HR_SENSOR_SCL_PIN, GPIO_PIN_RESET);
}

static void HR_Sensor_SDA_High(void)
{
	HAL_GPIO_WritePin(HR_SENSOR_SDA_PORT, HR_SENSOR_SDA_PIN, GPIO_PIN_SET);
}

static void HR_Sensor_SDA_Low(void)
{
	HAL_GPIO_WritePin(HR_SENSOR_SDA_PORT, HR_SENSOR_SDA_PIN, GPIO_PIN_RESET);
}

static GPIO_PinState HR_Sensor_SDA_Read(void)
{
	return HAL_GPIO_ReadPin(HR_SENSOR_SDA_PORT, HR_SENSOR_SDA_PIN);
}

static void HR_Sensor_I2C_Start(void)
{
	HR_Sensor_SDA_High();
	HR_Sensor_DelayUs(HR_SENSOR_SOFT_I2C_DELAY_US);
	HR_Sensor_SCL_High();
	HR_Sensor_DelayUs(HR_SENSOR_SOFT_I2C_DELAY_US);
	HR_Sensor_SDA_Low();
	HR_Sensor_DelayUs(HR_SENSOR_SOFT_I2C_DELAY_US);
	HR_Sensor_SCL_Low();
	HR_Sensor_DelayUs(HR_SENSOR_SOFT_I2C_DELAY_US);
}

static void HR_Sensor_I2C_Stop(void)
{
	HR_Sensor_SCL_Low();
	HR_Sensor_DelayUs(HR_SENSOR_SOFT_I2C_DELAY_US);
	HR_Sensor_SDA_Low();
	HR_Sensor_DelayUs(HR_SENSOR_SOFT_I2C_DELAY_US);
	HR_Sensor_SCL_High();
	HR_Sensor_DelayUs(HR_SENSOR_SOFT_I2C_DELAY_US);
	HR_Sensor_SDA_High();
	HR_Sensor_DelayUs(HR_SENSOR_SOFT_I2C_DELAY_US);
}

static void HR_Sensor_I2C_WriteBit(uint8_t bit_out)
{
	if (bit_out == 0U)
	{
		HR_Sensor_SDA_Low();
	}
	else
	{
		HR_Sensor_SDA_High();
	}

	HR_Sensor_DelayUs(HR_SENSOR_SOFT_I2C_DELAY_US);
	HR_Sensor_SCL_High();
	HR_Sensor_DelayUs(HR_SENSOR_SOFT_I2C_DELAY_US);
	HR_Sensor_SCL_Low();
}

static uint8_t HR_Sensor_I2C_ReadBit(void)
{
	uint8_t bit_in;

	HR_Sensor_SDA_High();
	HR_Sensor_DelayUs(HR_SENSOR_SOFT_I2C_DELAY_US);
	HR_Sensor_SCL_High();
	HR_Sensor_DelayUs(HR_SENSOR_SOFT_I2C_DELAY_US);
	bit_in = (HR_Sensor_SDA_Read() == GPIO_PIN_SET) ? 1U : 0U;
	HR_Sensor_SCL_Low();

	return bit_in;
}

static uint8_t HR_Sensor_I2C_WriteByte(uint8_t data)
{
	uint8_t bit_idx;

	for (bit_idx = 0U; bit_idx < 8U; bit_idx++)
	{
		HR_Sensor_I2C_WriteBit((uint8_t)((data & 0x80U) ? 1U : 0U));
		data <<= 1;
	}

	return HR_Sensor_I2C_ReadBit();
}

static uint8_t HR_Sensor_I2C_ReadByte(uint8_t ack_nack)
{
	uint8_t bit_idx;
	uint8_t data = 0U;

	for (bit_idx = 0U; bit_idx < 8U; bit_idx++)
	{
		data <<= 1;
		if (HR_Sensor_I2C_ReadBit() != 0U)
		{
			data |= 0x01U;
		}
	}

	HR_Sensor_I2C_WriteBit(ack_nack);
	return data;
}

static HAL_StatusTypeDef HR_Sensor_WriteRegInternal(uint8_t reg, uint8_t value)
{
	uint8_t attempt;
	uint8_t address = (uint8_t)(HR_SENSOR_MAX30102_ADDR << 1);

	for (attempt = 0U; attempt < HR_SENSOR_SOFT_I2C_RETRY; attempt++)
	{
		HR_Sensor_I2C_Stop();
		HR_Sensor_I2C_Start();

		if (HR_Sensor_I2C_WriteByte(address) != 0U)
		{
			HR_Sensor_I2C_Stop();
			continue;
		}

		if (HR_Sensor_I2C_WriteByte(reg) != 0U)
		{
			HR_Sensor_I2C_Stop();
			continue;
		}

		if (HR_Sensor_I2C_WriteByte(value) != 0U)
		{
			HR_Sensor_I2C_Stop();
			continue;
		}

		HR_Sensor_I2C_Stop();
		return HAL_OK;
	}

	return HAL_ERROR;
}

static HAL_StatusTypeDef HR_Sensor_ReadRegInternal(uint8_t reg, uint8_t *value)
{
	uint8_t attempt;
	uint8_t address = (uint8_t)(HR_SENSOR_MAX30102_ADDR << 1);

	if (value == NULL)
	{
		return HAL_ERROR;
	}

	for (attempt = 0U; attempt < HR_SENSOR_SOFT_I2C_RETRY; attempt++)
	{
		HR_Sensor_I2C_Stop();
		HR_Sensor_I2C_Start();

		if (HR_Sensor_I2C_WriteByte(address) != 0U)
		{
			HR_Sensor_I2C_Stop();
			continue;
		}

		if (HR_Sensor_I2C_WriteByte(reg) != 0U)
		{
			HR_Sensor_I2C_Stop();
			continue;
		}

		HR_Sensor_I2C_Start();
		if (HR_Sensor_I2C_WriteByte((uint8_t)(address | 0x01U)) != 0U)
		{
			HR_Sensor_I2C_Stop();
			continue;
		}

		*value = HR_Sensor_I2C_ReadByte(HR_SENSOR_NACK);
		HR_Sensor_I2C_Stop();
		return HAL_OK;
	}

	return HAL_ERROR;
}

static HAL_StatusTypeDef HR_Sensor_ReadBurstInternal(uint8_t reg, uint8_t *buffer, uint16_t length)
{
	uint16_t index;
	uint8_t attempt;
	uint8_t address = (uint8_t)(HR_SENSOR_MAX30102_ADDR << 1);

	if ((buffer == NULL) || (length == 0U))
	{
		return HAL_ERROR;
	}

	for (attempt = 0U; attempt < HR_SENSOR_SOFT_I2C_RETRY; attempt++)
	{
		HR_Sensor_I2C_Stop();
		HR_Sensor_I2C_Start();

		if (HR_Sensor_I2C_WriteByte(address) != 0U)
		{
			HR_Sensor_I2C_Stop();
			continue;
		}

		if (HR_Sensor_I2C_WriteByte(reg) != 0U)
		{
			HR_Sensor_I2C_Stop();
			continue;
		}

		HR_Sensor_I2C_Start();
		if (HR_Sensor_I2C_WriteByte((uint8_t)(address | 0x01U)) != 0U)
		{
			HR_Sensor_I2C_Stop();
			continue;
		}

		for (index = 0U; index < length; index++)
		{
			uint8_t ack = (index < (length - 1U)) ? HR_SENSOR_ACK : HR_SENSOR_NACK;
			buffer[index] = HR_Sensor_I2C_ReadByte(ack);
		}

		HR_Sensor_I2C_Stop();
		return HAL_OK;
	}

	return HAL_ERROR;
}
#endif

void HR_Sensor_Init(void)
{
	GPIO_InitTypeDef gpio_init = {0};

#if (HR_SENSOR_USE_HARDWARE_I2C == 1U)
	HR_Sensor_I2C_Init();
#else
	HR_Sensor_EnableDwtDelay();

	HR_Sensor_EnableGPIOClock(HR_SENSOR_SCL_PORT);
	if (HR_SENSOR_SDA_PORT != HR_SENSOR_SCL_PORT)
	{
		HR_Sensor_EnableGPIOClock(HR_SENSOR_SDA_PORT);
	}

	gpio_init.Mode = GPIO_MODE_OUTPUT_OD;
	gpio_init.Pull = GPIO_PULLUP;
	gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;

	if (HR_SENSOR_SCL_PORT == HR_SENSOR_SDA_PORT)
	{
		gpio_init.Pin = (uint16_t)(HR_SENSOR_SCL_PIN | HR_SENSOR_SDA_PIN);
		HAL_GPIO_Init(HR_SENSOR_SCL_PORT, &gpio_init);
	}
	else
	{
		gpio_init.Pin = HR_SENSOR_SCL_PIN;
		HAL_GPIO_Init(HR_SENSOR_SCL_PORT, &gpio_init);

		gpio_init.Pin = HR_SENSOR_SDA_PIN;
		HAL_GPIO_Init(HR_SENSOR_SDA_PORT, &gpio_init);
	}

	HR_Sensor_SCL_High();
	HR_Sensor_SDA_High();
#endif

	HR_Sensor_EnableGPIOClock(HR_SENSOR_INT_PORT);
	gpio_init.Pin = HR_SENSOR_INT_PIN;
	gpio_init.Mode = GPIO_MODE_INPUT;
	gpio_init.Pull = GPIO_PULLUP;
	gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(HR_SENSOR_INT_PORT, &gpio_init);

	(void)HR_Sensor_Reset();
	HAL_Delay(5U);
	(void)HR_Sensor_ConfigDefault();
}

uint8_t HR_Sensor_IsConnected(void)
{
	uint8_t part_id = 0U;

	if (HR_Sensor_ReadRegInternal(HR_SENSOR_REG_PART_ID, &part_id) != HAL_OK)
	{
		return 0U;
	}

	return (part_id == HR_SENSOR_MAX30102_PART_ID) ? 1U : 0U;
}

HAL_StatusTypeDef HR_Sensor_Reset(void)
{
	uint8_t mode = 0U;
	uint8_t retry;

	if (HR_Sensor_WriteRegInternal(HR_SENSOR_REG_MODE_CONFIG, 0x40U) != HAL_OK)
	{
		return HAL_ERROR;
	}

	for (retry = 0U; retry < 30U; retry++)
	{
		HAL_Delay(2U);
		if (HR_Sensor_ReadRegInternal(HR_SENSOR_REG_MODE_CONFIG, &mode) != HAL_OK)
		{
			continue;
		}
		if ((mode & 0x40U) == 0U)
		{
			return HAL_OK;
		}
	}

	return HAL_TIMEOUT;
}

HAL_StatusTypeDef HR_Sensor_ConfigDefault(void)
{
	uint8_t status_dummy;

	if (HR_Sensor_WriteRegInternal(HR_SENSOR_REG_INTR_ENABLE_1, 0x40U) != HAL_OK)
	{
		return HAL_ERROR;
	}
	if (HR_Sensor_WriteRegInternal(HR_SENSOR_REG_INTR_ENABLE_2, 0x00U) != HAL_OK)
	{
		return HAL_ERROR;
	}

	if (HR_Sensor_WriteRegInternal(HR_SENSOR_REG_FIFO_WR_PTR, 0x00U) != HAL_OK)
	{
		return HAL_ERROR;
	}
	if (HR_Sensor_WriteRegInternal(HR_SENSOR_REG_OVF_COUNTER, 0x00U) != HAL_OK)
	{
		return HAL_ERROR;
	}
	if (HR_Sensor_WriteRegInternal(HR_SENSOR_REG_FIFO_RD_PTR, 0x00U) != HAL_OK)
	{
		return HAL_ERROR;
	}

	if (HR_Sensor_WriteRegInternal(HR_SENSOR_REG_FIFO_CONFIG, 0x0FU) != HAL_OK)
	{
		return HAL_ERROR;
	}

	if (HR_Sensor_WriteRegInternal(HR_SENSOR_REG_MODE_CONFIG, 0x03U) != HAL_OK)
	{
		return HAL_ERROR;
	}
	if (HR_Sensor_WriteRegInternal(HR_SENSOR_REG_SPO2_CONFIG, 0x27U) != HAL_OK)
	{
		return HAL_ERROR;
	}

	if (HR_Sensor_SetLedCurrent(0x24U, 0x24U) != HAL_OK)
	{
		return HAL_ERROR;
	}

	if (HR_Sensor_WriteRegInternal(HR_SENSOR_REG_PILOT_PA, 0x7FU) != HAL_OK)
	{
		return HAL_ERROR;
	}
	if (HR_Sensor_WriteRegInternal(HR_SENSOR_REG_MULTI_LED_CTRL1, 0x21U) != HAL_OK)
	{
		return HAL_ERROR;
	}
	if (HR_Sensor_WriteRegInternal(HR_SENSOR_REG_MULTI_LED_CTRL2, 0x00U) != HAL_OK)
	{
		return HAL_ERROR;
	}

	(void)HR_Sensor_ReadRegInternal(HR_SENSOR_REG_INTR_STATUS_1, &status_dummy);
	(void)HR_Sensor_ReadRegInternal(HR_SENSOR_REG_INTR_STATUS_2, &status_dummy);

	return HAL_OK;
}

HAL_StatusTypeDef HR_Sensor_SetLedCurrent(uint8_t red_pa, uint8_t ir_pa)
{
	if (HR_Sensor_WriteRegInternal(HR_SENSOR_REG_LED1_PA, red_pa) != HAL_OK)
	{
		return HAL_ERROR;
	}
	if (HR_Sensor_WriteRegInternal(HR_SENSOR_REG_LED2_PA, ir_pa) != HAL_OK)
	{
		return HAL_ERROR;
	}

	return HAL_OK;
}

HAL_StatusTypeDef HR_Sensor_ReadSample(HR_SensorSample_t *sample)
{
	uint8_t fifo_raw[HR_SENSOR_FIFO_BYTES_PER_SAMPLE];

	if (sample == NULL)
	{
		return HAL_ERROR;
	}

	if (HR_Sensor_ReadBurstInternal(HR_SENSOR_REG_FIFO_DATA, fifo_raw, HR_SENSOR_FIFO_BYTES_PER_SAMPLE) != HAL_OK)
	{
		sample->red = HR_SENSOR_INVALID_RAW;
		sample->ir = HR_SENSOR_INVALID_RAW;
		return HAL_ERROR;
	}

	sample->red =
		((((uint32_t)fifo_raw[0] << 16) | ((uint32_t)fifo_raw[1] << 8) | fifo_raw[2]) & 0x03FFFFUL);
	sample->ir =
		((((uint32_t)fifo_raw[3] << 16) | ((uint32_t)fifo_raw[4] << 8) | fifo_raw[5]) & 0x03FFFFUL);

	return HAL_OK;
}

HAL_StatusTypeDef HR_Sensor_ReadRegister(uint8_t reg, uint8_t *value)
{
	return HR_Sensor_ReadRegInternal(reg, value);
}

HAL_StatusTypeDef HR_Sensor_WriteRegister(uint8_t reg, uint8_t value)
{
	return HR_Sensor_WriteRegInternal(reg, value);
}

void MAX30102_Init(void)
{
	HR_Sensor_Init();
}

HAL_StatusTypeDef MAX30102_ReadRaw(uint32_t *red, uint32_t *ir)
{
	HR_SensorSample_t sample;

	if ((red == NULL) || (ir == NULL))
	{
		return HAL_ERROR;
	}

	if (HR_Sensor_ReadSample(&sample) != HAL_OK)
	{
		*red = HR_SENSOR_INVALID_RAW;
		*ir = HR_SENSOR_INVALID_RAW;
		return HAL_ERROR;
	}

	*red = sample.red;
	*ir = sample.ir;
	return HAL_OK;
}
