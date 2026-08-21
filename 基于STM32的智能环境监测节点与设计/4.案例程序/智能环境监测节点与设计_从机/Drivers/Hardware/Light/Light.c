#include "./Hardware/Light/Light.h"
#include "./SYSTEM/delay/delay.h"

static uint8_t g_bh1750_mode = BH1750_DEFAULT_MODE;

#if BH1750_USE_HARDWARE_I2C
static I2C_HandleTypeDef g_bh1750_i2c_handle;
#endif

static void BH1750_EnableGPIOClock(GPIO_TypeDef *port)
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
#if defined(GPIOD) && defined(__HAL_RCC_GPIOD_CLK_ENABLE)
	else if (port == GPIOD)
	{
		__HAL_RCC_GPIOD_CLK_ENABLE();
	}
#endif
#if defined(GPIOE) && defined(__HAL_RCC_GPIOE_CLK_ENABLE)
	else if (port == GPIOE)
	{
		__HAL_RCC_GPIOE_CLK_ENABLE();
	}
#endif
}

static uint8_t BH1750_GetDeviceAddress(void)
{
#if BH1750_ADDR_SELECT_BY_PIN
	if (HAL_GPIO_ReadPin(BH1750_I2C_ADDR_PORT, BH1750_I2C_ADDR_PIN) == GPIO_PIN_SET)
	{
		return BH1750_I2C_HIGH_ADDR;
	}
	return BH1750_I2C_LOW_ADDR;
#else
	return BH1750_FIXED_ADDR;
#endif
}

static uint16_t BH1750_GetMeasureDelayMs(uint8_t mode)
{
	if ((mode == BH1750_CMD_CONT_L_RES_MODE) || (mode == BH1750_CMD_ONE_L_RES_MODE))
	{
		return BH1750_MEASURE_DELAY_L_RES_MS;
	}
	return BH1750_MEASURE_DELAY_H_RES_MS;
}

#if !BH1750_USE_HARDWARE_I2C

static void BH1750_SoftI2CDelay(void)
{
	delay_us(BH1750_SOFT_I2C_DELAY_US);
}

static void BH1750_SDA_SetOutput(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	GPIO_InitStruct.Pin = BH1750_I2C_SDA_PIN;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
	GPIO_InitStruct.Pull = GPIO_PULLUP;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
	HAL_GPIO_Init(BH1750_I2C_SDA_PORT, &GPIO_InitStruct);
}

static void BH1750_SDA_SetInput(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	GPIO_InitStruct.Pin = BH1750_I2C_SDA_PIN;
	GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	GPIO_InitStruct.Pull = GPIO_PULLUP;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
	HAL_GPIO_Init(BH1750_I2C_SDA_PORT, &GPIO_InitStruct);
}

static void BH1750_SCL_Write(uint8_t level)
{
	HAL_GPIO_WritePin(BH1750_I2C_SCL_PORT,
					  BH1750_I2C_SCL_PIN,
					  (level != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void BH1750_SDA_Write(uint8_t level)
{
	HAL_GPIO_WritePin(BH1750_I2C_SDA_PORT,
					  BH1750_I2C_SDA_PIN,
					  (level != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static uint8_t BH1750_SDA_Read(void)
{
	return (uint8_t)HAL_GPIO_ReadPin(BH1750_I2C_SDA_PORT, BH1750_I2C_SDA_PIN);
}

static void BH1750_SoftI2C_Start(void)
{
	BH1750_SDA_SetOutput();
	BH1750_SDA_Write(1U);
	BH1750_SCL_Write(1U);
	BH1750_SoftI2CDelay();
	BH1750_SDA_Write(0U);
	BH1750_SoftI2CDelay();
	BH1750_SCL_Write(0U);
}

static void BH1750_SoftI2C_Stop(void)
{
	BH1750_SDA_SetOutput();
	BH1750_SDA_Write(0U);
	BH1750_SoftI2CDelay();
	BH1750_SCL_Write(1U);
	BH1750_SoftI2CDelay();
	BH1750_SDA_Write(1U);
	BH1750_SoftI2CDelay();
}

static uint8_t BH1750_SoftI2C_WaitAck(void)
{
	uint16_t timeout = 0U;

	BH1750_SDA_SetInput();
	BH1750_SDA_Write(1U);
	BH1750_SoftI2CDelay();
	BH1750_SCL_Write(1U);
	BH1750_SoftI2CDelay();

	while (BH1750_SDA_Read() != 0U)
	{
		timeout++;
		if (timeout > 300U)
		{
			BH1750_SoftI2C_Stop();
			return 0U;
		}
	}

	BH1750_SCL_Write(0U);
	return 1U;
}

static void BH1750_SoftI2C_Ack(void)
{
	BH1750_SDA_SetOutput();
	BH1750_SDA_Write(0U);
	BH1750_SoftI2CDelay();
	BH1750_SCL_Write(1U);
	BH1750_SoftI2CDelay();
	BH1750_SCL_Write(0U);
}

static void BH1750_SoftI2C_NAck(void)
{
	BH1750_SDA_SetOutput();
	BH1750_SDA_Write(1U);
	BH1750_SoftI2CDelay();
	BH1750_SCL_Write(1U);
	BH1750_SoftI2CDelay();
	BH1750_SCL_Write(0U);
}

static void BH1750_SoftI2C_WriteByte(uint8_t data)
{
	uint8_t i = 0U;

	BH1750_SDA_SetOutput();
	for (i = 0U; i < 8U; i++)
	{
		BH1750_SDA_Write((uint8_t)((data & 0x80U) != 0U));
		BH1750_SoftI2CDelay();
		BH1750_SCL_Write(1U);
		BH1750_SoftI2CDelay();
		BH1750_SCL_Write(0U);
		data <<= 1;
	}
}

static uint8_t BH1750_SoftI2C_ReadByte(void)
{
	uint8_t i = 0U;
	uint8_t data = 0U;

	BH1750_SDA_SetInput();
	for (i = 0U; i < 8U; i++)
	{
		data <<= 1;
		BH1750_SCL_Write(1U);
		BH1750_SoftI2CDelay();
		if (BH1750_SDA_Read() != 0U)
		{
			data |= 0x01U;
		}
		BH1750_SCL_Write(0U);
		BH1750_SoftI2CDelay();
	}

	return data;
}

#else

static uint8_t BH1750_HardwareI2C_Init(void)
{
#if BH1750_HARDWARE_I2C_INDEX == 1U
	__HAL_RCC_I2C1_CLK_ENABLE();
#if BH1750_HARDWARE_I2C_REMAP
	__HAL_RCC_AFIO_CLK_ENABLE();
	__HAL_AFIO_REMAP_I2C1_ENABLE();
#endif
	g_bh1750_i2c_handle.Instance = I2C1;
#elif BH1750_HARDWARE_I2C_INDEX == 2U
	__HAL_RCC_I2C2_CLK_ENABLE();
	g_bh1750_i2c_handle.Instance = I2C2;
#else
#error "BH1750_HARDWARE_I2C_INDEX only supports 1 or 2"
#endif

	g_bh1750_i2c_handle.Init.ClockSpeed = BH1750_I2C_CLOCK_SPEED;
	g_bh1750_i2c_handle.Init.DutyCycle = I2C_DUTYCYCLE_2;
	g_bh1750_i2c_handle.Init.OwnAddress1 = 0U;
	g_bh1750_i2c_handle.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
	g_bh1750_i2c_handle.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
	g_bh1750_i2c_handle.Init.OwnAddress2 = 0U;
	g_bh1750_i2c_handle.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
	g_bh1750_i2c_handle.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

	if (HAL_I2C_Init(&g_bh1750_i2c_handle) != HAL_OK)
	{
		return BH1750_ERR_TIMEOUT;
	}

	return BH1750_OK;
}

#endif

static void BH1750_GPIO_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	BH1750_EnableGPIOClock(BH1750_I2C_SCL_PORT);
	BH1750_EnableGPIOClock(BH1750_I2C_SDA_PORT);
	BH1750_EnableGPIOClock(BH1750_I2C_ADDR_PORT);

#if BH1750_ADDR_SELECT_BY_PIN
	GPIO_InitStruct.Pin = BH1750_I2C_ADDR_PIN;
	GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	GPIO_InitStruct.Pull = GPIO_PULLUP;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
	HAL_GPIO_Init(BH1750_I2C_ADDR_PORT, &GPIO_InitStruct);
#endif

#if BH1750_USE_HARDWARE_I2C
	GPIO_InitStruct.Pin = BH1750_I2C_SCL_PIN;
	GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
	GPIO_InitStruct.Pull = GPIO_PULLUP;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
	HAL_GPIO_Init(BH1750_I2C_SCL_PORT, &GPIO_InitStruct);

	GPIO_InitStruct.Pin = BH1750_I2C_SDA_PIN;
	HAL_GPIO_Init(BH1750_I2C_SDA_PORT, &GPIO_InitStruct);
#else
	GPIO_InitStruct.Pin = BH1750_I2C_SCL_PIN;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
	GPIO_InitStruct.Pull = GPIO_PULLUP;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
	HAL_GPIO_Init(BH1750_I2C_SCL_PORT, &GPIO_InitStruct);

	GPIO_InitStruct.Pin = BH1750_I2C_SDA_PIN;
	HAL_GPIO_Init(BH1750_I2C_SDA_PORT, &GPIO_InitStruct);

	HAL_GPIO_WritePin(BH1750_I2C_SCL_PORT, BH1750_I2C_SCL_PIN, GPIO_PIN_SET);
	HAL_GPIO_WritePin(BH1750_I2C_SDA_PORT, BH1750_I2C_SDA_PIN, GPIO_PIN_SET);
#endif
}

static uint8_t BH1750_WriteCommand(uint8_t command)
{
#if BH1750_USE_HARDWARE_I2C
	uint16_t dev_addr = (uint16_t)(BH1750_GetDeviceAddress() << 1);

	if (HAL_I2C_Master_Transmit(&g_bh1750_i2c_handle,
								dev_addr,
								&command,
								1U,
								BH1750_I2C_TIMEOUT) != HAL_OK)
	{
		return BH1750_ERR_TIMEOUT;
	}
	return BH1750_OK;
#else
	uint8_t dev_addr = (uint8_t)(BH1750_GetDeviceAddress() << 1);

	BH1750_SoftI2C_Start();
	BH1750_SoftI2C_WriteByte(dev_addr);
	if (BH1750_SoftI2C_WaitAck() == 0U)
	{
		return BH1750_ERR_I2C_ACK;
	}

	BH1750_SoftI2C_WriteByte(command);
	if (BH1750_SoftI2C_WaitAck() == 0U)
	{
		return BH1750_ERR_I2C_ACK;
	}

	BH1750_SoftI2C_Stop();
	return BH1750_OK;
#endif
}

static uint8_t BH1750_ReadBytes(uint8_t *data, uint8_t len)
{
	if ((data == NULL) || (len == 0U))
	{
		return BH1750_ERR_PARAM;
	}

#if BH1750_USE_HARDWARE_I2C
	uint16_t dev_addr = (uint16_t)(BH1750_GetDeviceAddress() << 1);

	if (HAL_I2C_Master_Receive(&g_bh1750_i2c_handle,
							   dev_addr,
							   data,
							   len,
							   BH1750_I2C_TIMEOUT) != HAL_OK)
	{
		return BH1750_ERR_TIMEOUT;
	}
	return BH1750_OK;
#else
	uint8_t i = 0U;
	uint8_t dev_addr = (uint8_t)((BH1750_GetDeviceAddress() << 1) | 0x01U);

	BH1750_SoftI2C_Start();
	BH1750_SoftI2C_WriteByte(dev_addr);
	if (BH1750_SoftI2C_WaitAck() == 0U)
	{
		return BH1750_ERR_I2C_ACK;
	}

	for (i = 0U; i < len; i++)
	{
		data[i] = BH1750_SoftI2C_ReadByte();
		if (i < (len - 1U))
		{
			BH1750_SoftI2C_Ack();
		}
		else
		{
			BH1750_SoftI2C_NAck();
		}
	}

	BH1750_SoftI2C_Stop();
	return BH1750_OK;
#endif
}

static uint8_t BH1750_IsMeasureModeValid(uint8_t mode)
{
	if ((mode == BH1750_CMD_CONT_H_RES_MODE) ||
		(mode == BH1750_CMD_CONT_H_RES2_MODE) ||
		(mode == BH1750_CMD_CONT_L_RES_MODE) ||
		(mode == BH1750_CMD_ONE_H_RES_MODE) ||
		(mode == BH1750_CMD_ONE_H_RES2_MODE) ||
		(mode == BH1750_CMD_ONE_L_RES_MODE))
	{
		return 1U;
	}

	return 0U;
}

uint8_t BH1750_Init(void)
{
	uint8_t status = BH1750_OK;

	BH1750_GPIO_Init();

#if BH1750_USE_HARDWARE_I2C
	status = BH1750_HardwareI2C_Init();
	if (status != BH1750_OK)
	{
		return status;
	}
#endif

	delay_ms(10);

	status = BH1750_WriteCommand(BH1750_CMD_POWER_ON);
	if (status != BH1750_OK)
	{
		return status;
	}

	status = BH1750_WriteCommand(BH1750_CMD_RESET);
	if (status != BH1750_OK)
	{
		return status;
	}

	g_bh1750_mode = BH1750_DEFAULT_MODE;
	return BH1750_SetMode(g_bh1750_mode);
}

uint8_t BH1750_SetMode(uint8_t mode)
{
	uint8_t status = BH1750_OK;

	if (BH1750_IsMeasureModeValid(mode) == 0U)
	{
		return BH1750_ERR_PARAM;
	}

	status = BH1750_WriteCommand(mode);
	if (status != BH1750_OK)
	{
		return status;
	}

	g_bh1750_mode = mode;
	return BH1750_OK;
}

uint8_t BH1750_ReadRaw(uint16_t *raw_data)
{
	uint8_t data[2] = {0U};
	uint8_t status = BH1750_OK;

	if (raw_data == NULL)
	{
		return BH1750_ERR_PARAM;
	}

	status = BH1750_WriteCommand(g_bh1750_mode);
	if (status != BH1750_OK)
	{
		return status;
	}

	delay_ms(BH1750_GetMeasureDelayMs(g_bh1750_mode));

	status = BH1750_ReadBytes(data, 2U);
	if (status != BH1750_OK)
	{
		return status;
	}

	*raw_data = (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
	return BH1750_OK;
}

uint8_t BH1750_ReadLux(float *lux)
{
	uint16_t raw_data = 0U;
	uint8_t status = BH1750_OK;

	if (lux == NULL)
	{
		return BH1750_ERR_PARAM;
	}

	status = BH1750_ReadRaw(&raw_data);
	if (status != BH1750_OK)
	{
		return status;
	}

	*lux = (float)raw_data / 1.2f;
	return BH1750_OK;
}

float BH1750_GetLux(void)
{
	float lux = -1.0f;

	if (BH1750_ReadLux(&lux) != BH1750_OK)
	{
		return -1.0f;
	}

	return lux;
}