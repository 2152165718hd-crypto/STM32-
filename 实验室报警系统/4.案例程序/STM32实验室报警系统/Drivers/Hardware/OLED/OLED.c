
#include ".\Hardware\OLED\OLED.h"
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdarg.h>

/*全局变量*********************/

#if OLED_USE_HARDWARE_I2C
/* I2C1句柄 */
I2C_HandleTypeDef hi2c1;
#if OLED_USE_DMA
#if OLED_DMA_TRANSPORT_MODE
DMA_HandleTypeDef hdma_i2c1_tx;
#else
DMA_HandleTypeDef hdma_oled_memcpy;
#endif
#endif
#endif

/**
 * OLED显存数组
 * 所有的显示函数，都只是对此显存数组进行读写
 * 随后调用OLED_Update函数或OLED_UpdateArea函数
 * 才会将显存数组的数据发送到OLED硬件，进行显示
 */
uint8_t OLED_DisplayBuf[8][128];
#if OLED_ENABLE_DIFF_UPDATE
static uint8_t OLED_LastDisplayBuf[8][128];
static uint8_t OLED_NeedFullRefresh = 1;
#endif

/*********************全局变量*/

/*引脚配置*********************/

#if !OLED_USE_HARDWARE_I2C

/**
 * 函数：OLED写SCL高低电平
 * 参数：要写入SCL的电平值，范围0/1
 * 返回值：无
 * 说明：当上层函数需要写SCL时，此函数会被调用
 *           用户需要根据参数传入的值，将SCL置为高电平或者低电平
 *           当参数传0时，置SCL为低电平，当参数传入1时，置SCL为高电平
 */
void OLED_W_SCL(uint8_t BitValue)
{
	/*根据BitValue的值，将SCL置高电平或者低电平*/
	HAL_GPIO_WritePin(OLED_SCL_PORT, OLED_SCL_PIN, (GPIO_PinState)BitValue);

	/*如果单片机速度过快，可在此添加适量延时，以避免超出I2C通信的最大速度*/
	//...
}

/**
 * 函数：OLED写SDA高低电平
 * 参数：要写入SDA的电平值，范围0/1
 * 返回值：无
 * 说明：当上层函数需要写SDA时，此函数会被调用
 *           用户需要根据参数传入的值，将SDA置为高电平或者低电平
 *           当参数传0时，置SDA为低电平，当参数传入1时，置SDA为高电平
 */
void OLED_W_SDA(uint8_t BitValue)
{
	/*根据BitValue的值，将SDA置高电平或者低电平*/
	HAL_GPIO_WritePin(OLED_SDA_PORT, OLED_SDA_PIN, (GPIO_PinState)BitValue);

	/*如果单片机速度过快，可在此添加适量延时，以避免超出I2C通信的最大速度*/
	//...
}

/**
 * 函数：OLED引脚初始化（软件I2C
 * 参数：
 * 返回值：无
 * 说明：当上层函数需要初始化时，此函数会被调用
 *           用户需要将SCL和SDA引脚初始化为开漏模式，并释放引脚
 */
void OLED_GPIO_Init(void)
{
	uint32_t i, j;

	/*在初始化前，加入适量延时，待OLED供电稳定*/
	for (i = 0; i < 1000; i++)
	{
		for (j = 0; j < 1000; j++)
			;
	}
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	/*将SCL和SDA引脚初始化为开漏模式*/
	__HAL_RCC_GPIOB_CLK_ENABLE();
	GPIO_InitStruct.Pin = OLED_SCL_PIN | OLED_SDA_PIN;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
	HAL_GPIO_Init(OLED_SCL_PORT, &GPIO_InitStruct);
	HAL_GPIO_Init(OLED_SDA_PORT, &GPIO_InitStruct);
	/*释放SCL和SDA*/
	OLED_W_SCL(1);
	OLED_W_SDA(1);
}

#else

/**
 * 函数：硬件I2C1初始化
 * 参数：
 * 返回值：无
 * 说明：配置I2C1，使用PB6(SCL)和PB7(SDA)
 */
#if OLED_USE_DMA
void HAL_I2C_MspInit(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance == I2C1)
    {
        __HAL_RCC_DMA1_CLK_ENABLE();
#if OLED_DMA_TRANSPORT_MODE
        hdma_i2c1_tx.Instance = DMA1_Channel6;
        hdma_i2c1_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
        hdma_i2c1_tx.Init.PeriphInc = DMA_PINC_DISABLE;
        hdma_i2c1_tx.Init.MemInc = DMA_MINC_ENABLE;
        hdma_i2c1_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
        hdma_i2c1_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
        hdma_i2c1_tx.Init.Mode = DMA_NORMAL;
        hdma_i2c1_tx.Init.Priority = DMA_PRIORITY_HIGH;
        if (HAL_DMA_Init(&hdma_i2c1_tx) != HAL_OK)
        {
            while (1)
                ;
        }
        __HAL_LINKDMA(hi2c, hdmatx, hdma_i2c1_tx);

        HAL_NVIC_SetPriority(DMA1_Channel6_IRQn, 1, 0);
        HAL_NVIC_EnableIRQ(DMA1_Channel6_IRQn);
        HAL_NVIC_SetPriority(I2C1_EV_IRQn, 1, 1);
        HAL_NVIC_EnableIRQ(I2C1_EV_IRQn);
        HAL_NVIC_SetPriority(I2C1_ER_IRQn, 1, 2);
        HAL_NVIC_EnableIRQ(I2C1_ER_IRQn);
#else
        hdma_oled_memcpy.Instance = DMA1_Channel1;
        hdma_oled_memcpy.Init.Direction = DMA_MEMORY_TO_MEMORY;
        hdma_oled_memcpy.Init.PeriphInc = DMA_PINC_ENABLE;
        hdma_oled_memcpy.Init.MemInc = DMA_MINC_ENABLE;
        hdma_oled_memcpy.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
        hdma_oled_memcpy.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
        hdma_oled_memcpy.Init.Mode = DMA_NORMAL;
        hdma_oled_memcpy.Init.Priority = DMA_PRIORITY_HIGH;
        if (HAL_DMA_Init(&hdma_oled_memcpy) != HAL_OK)
        {
            while (1)
                ;
        }
#endif
    }
}
#endif

#if OLED_USE_DMA && OLED_DMA_TRANSPORT_MODE
void DMA1_Channel6_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_i2c1_tx);
}

void I2C1_EV_IRQHandler(void)
{
    HAL_I2C_EV_IRQHandler(&OLED_I2C_INSTANCE);
}

void I2C1_ER_IRQHandler(void)
{
    HAL_I2C_ER_IRQHandler(&OLED_I2C_INSTANCE);
}
#endif

static void OLED_I2C1_Init(void)
{
	/* I2C1外设时钟使能 */
	__HAL_RCC_I2C1_CLK_ENABLE();
	__HAL_RCC_GPIOB_CLK_ENABLE();

	/* 配置I2C引脚 PB6(SCL), PB7(SDA) */
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	/**I2C1 GPIO Configuration
	PB6     ------> I2C1_SCL
	PB7     ------> I2C1_SDA
	*/
	GPIO_InitStruct.Pin = OLED_SCL_PIN | OLED_SDA_PIN;
	GPIO_InitStruct.Mode = GPIO_MODE_AF_OD; // 开漏复用模
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
	HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

	/* 配置I2C参数 */
	hi2c1.Instance = I2C1;
	hi2c1.Init.ClockSpeed = OLED_I2C_CLOCK_SPEED; // 400kHz快速模式
	hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
	hi2c1.Init.OwnAddress1 = 0;
	hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
	hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
	hi2c1.Init.OwnAddress2 = 0;
	hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
	hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

	/* 初始化I2C */
	if (HAL_I2C_Init(&hi2c1) != HAL_OK)
	{
		/* 初始化失败处*/
		while (1)
			;
	}
}

/**
 * 函数：OLED硬件I2C初始化（I2C1
 * 参数：
 * 返回值：无
 * 说明：初始化I2C1硬件，使用PB6(SCL)和PB7(SDA)
 */
void OLED_GPIO_Init(void)
{
	uint32_t i, j;

	/*在初始化前，加入适量延时，待OLED供电稳定*/
	for (i = 0; i < 1000; i++)
	{
		for (j = 0; j < 1000; j++)
			;
	}

	/* 初始化硬件I2C */
	OLED_I2C1_Init();
}

#endif

/*********************引脚配置*/

/*通信协议*********************/

#if !OLED_USE_HARDWARE_I2C

void OLED_I2C_Start(void)
{
    OLED_W_SDA(1);
    OLED_W_SCL(1);
    OLED_W_SDA(0);
    OLED_W_SCL(0);
}

void OLED_I2C_Stop(void)
{
    OLED_W_SDA(0);
    OLED_W_SCL(1);
    OLED_W_SDA(1);
}

void OLED_I2C_SendByte(uint8_t Byte)
{
    uint8_t i;
    for (i = 0; i < 8; i++)
    {
        OLED_W_SDA(!!(Byte & (0x80 >> i)));
        OLED_W_SCL(1);
        OLED_W_SCL(0);
    }
    OLED_W_SCL(1);
    OLED_W_SCL(0);
}

void OLED_WriteCommand(uint8_t Command)
{
    OLED_I2C_Start();
    OLED_I2C_SendByte(0x78);
    OLED_I2C_SendByte(0x00);
    OLED_I2C_SendByte(Command);
    OLED_I2C_Stop();
}

void OLED_WriteData(uint8_t *Data, uint8_t Count)
{
    uint8_t i;
    OLED_I2C_Start();
    OLED_I2C_SendByte(0x78);
    OLED_I2C_SendByte(0x40);
    for (i = 0; i < Count; i++)
    {
        OLED_I2C_SendByte(Data[i]);
    }
    OLED_I2C_Stop();
}

#else

static uint8_t OLED_CmdTxBuf[4];
static uint8_t OLED_DataTxBuf[2][129];
static uint8_t OLED_DataTxIndex = 0;

static void OLED_WriteCommandList(const uint8_t *Commands, uint8_t Count)
{
    uint8_t i;
    if (Count == 0)
    {
        return;
    }
    OLED_CmdTxBuf[0] = 0x00;
    for (i = 0; i < Count; i++)
    {
        OLED_CmdTxBuf[i + 1] = Commands[i];
    }
    (void)HAL_I2C_Master_Transmit(&OLED_I2C_INSTANCE, OLED_I2C_ADDR, OLED_CmdTxBuf, Count + 1, OLED_I2C_TIMEOUT);
}

#if OLED_USE_DMA && OLED_DMA_TRANSPORT_MODE
static volatile uint8_t OLED_I2C_DMA_TxDone = 0;
static volatile uint8_t OLED_I2C_DMA_Error = 0;

static HAL_StatusTypeDef OLED_WaitI2CReady(uint32_t Timeout)
{
    uint32_t TickStart = HAL_GetTick();
    while (HAL_I2C_GetState(&OLED_I2C_INSTANCE) != HAL_I2C_STATE_READY)
    {
        if ((HAL_GetTick() - TickStart) > Timeout)
        {
            return HAL_TIMEOUT;
        }
    }
    return HAL_OK;
}

static void OLED_I2C_Recover(void)
{
    (void)HAL_I2C_DeInit(&OLED_I2C_INSTANCE);
    (void)HAL_I2C_Init(&OLED_I2C_INSTANCE);
#if OLED_ENABLE_DIFF_UPDATE
    OLED_NeedFullRefresh = 1;
#endif
}

void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance == I2C1)
    {
        OLED_I2C_DMA_TxDone = 1;
    }
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance == I2C1)
    {
        OLED_I2C_DMA_Error = 1;
    }
}

static HAL_StatusTypeDef OLED_WriteDataDMA(uint8_t *Data, uint16_t Size)
{
    uint32_t TickStart;

    if (HAL_I2C_GetState(&OLED_I2C_INSTANCE) != HAL_I2C_STATE_READY)
    {
        if (OLED_WaitI2CReady(OLED_I2C_TIMEOUT) != HAL_OK)
        {
            OLED_I2C_Recover();
            return HAL_TIMEOUT;
        }
    }

    OLED_I2C_DMA_TxDone = 0;
    OLED_I2C_DMA_Error = 0;
    if (HAL_I2C_Master_Transmit_DMA(&OLED_I2C_INSTANCE, OLED_I2C_ADDR, Data, Size) != HAL_OK)
    {
        return HAL_ERROR;
    }

    TickStart = HAL_GetTick();
    while ((OLED_I2C_DMA_TxDone == 0U) && (OLED_I2C_DMA_Error == 0U))
    {
        if ((HAL_GetTick() - TickStart) > OLED_I2C_TIMEOUT)
        {
            OLED_I2C_Recover();
            return HAL_TIMEOUT;
        }
    }

    if (OLED_I2C_DMA_Error != 0U)
    {
        OLED_I2C_Recover();
        return HAL_ERROR;
    }

    if (OLED_WaitI2CReady(OLED_I2C_TIMEOUT) != HAL_OK)
    {
        OLED_I2C_Recover();
        return HAL_TIMEOUT;
    }

    return HAL_OK;
}
#elif OLED_USE_DMA
static void OLED_CopyDataByDMA(uint8_t *Dst, const uint8_t *Src, uint16_t Size)
{
    if ((Dst == NULL) || (Src == NULL) || (Size == 0U))
    {
        return;
    }

    if (HAL_DMA_Start(&hdma_oled_memcpy, (uint32_t)Src, (uint32_t)Dst, Size) == HAL_OK)
    {
        if (HAL_DMA_PollForTransfer(&hdma_oled_memcpy, HAL_DMA_FULL_TRANSFER, OLED_DMA_WAIT_TIMEOUT) == HAL_OK)
        {
            return;
        }
        (void)HAL_DMA_Abort(&hdma_oled_memcpy);
    }

    memcpy(Dst, Src, Size);
}
#endif

void OLED_WriteCommand(uint8_t Command)
{
    OLED_WriteCommandList(&Command, 1);
}

void OLED_WriteData(uint8_t *Data, uint8_t Count)
{
    uint8_t i;
    uint8_t *TxBuf;
    if (Count == 0)
    {
        return;
    }

    TxBuf = OLED_DataTxBuf[OLED_DataTxIndex];
    OLED_DataTxIndex ^= 1;

        TxBuf[0] = 0x40;

#if OLED_USE_DMA && !OLED_DMA_TRANSPORT_MODE
    (void)i;
    OLED_CopyDataByDMA(&TxBuf[1], Data, Count);
#else
    for (i = 0; i < Count; i++)
    {
        TxBuf[i + 1] = Data[i];
    }
#endif

#if OLED_USE_DMA && OLED_DMA_TRANSPORT_MODE
    if (OLED_WriteDataDMA(TxBuf, (uint16_t)(Count + 1U)) == HAL_OK)
    {
        return;
    }
#endif
    (void)HAL_I2C_Master_Transmit(&OLED_I2C_INSTANCE, OLED_I2C_ADDR, TxBuf, Count + 1, OLED_I2C_TIMEOUT);
}

#endif

/*********************通信协议*/

/*硬件配置*********************/

/**
 * 函数：OLED初始化
 * 参数：
 * 返回值：无
 * 说明：使用前，需要调用此初始化函
 */
void OLED_Init(void)
{
	OLED_GPIO_Init(); // 先调用底层的端口初始化

	/*写入一系列的命令，对OLED进行初始化配*/
	OLED_WriteCommand(0xAE); // 设置显示开关闭0xAE关闭0xAF开

	OLED_WriteCommand(0xD5); // 设置显示时钟分频振荡器频
	OLED_WriteCommand(0x80); // 0x00~0xFF

	OLED_WriteCommand(0xA8); // 设置多路复用
	OLED_WriteCommand(0x3F); // 0x0E~0x3F

	OLED_WriteCommand(0xD3); // 设置显示偏移
	OLED_WriteCommand(0x00); // 0x00~0x7F

	OLED_WriteCommand(0x40); // 设置显示开始行0x40~0x7F

	OLED_WriteCommand(0xA1); // 设置左右方向0xA1正常0xA0左右反置

	OLED_WriteCommand(0xC8); // 设置上下方向0xC8正常0xC0上下反置

	OLED_WriteCommand(0xDA); // 设置COM引脚硬件配置
	OLED_WriteCommand(0x12);

	OLED_WriteCommand(0x81); // 设置对比
	OLED_WriteCommand(0xCF); // 0x00~0xFF

	OLED_WriteCommand(0xD9); // 设置预充电周
	OLED_WriteCommand(0xF1);

	OLED_WriteCommand(0xDB); // 设置VCOMH取消选择级别
	OLED_WriteCommand(0x30);

	OLED_WriteCommand(0xA4); // 设置整个显示打开/关闭

	OLED_WriteCommand(0xA6); // 设置正常/反色显示0xA6正常0xA7反色

	OLED_WriteCommand(0x8D); // 设置充电
	OLED_WriteCommand(0x14);

	OLED_WriteCommand(0xAF); // 开启显

#if OLED_ENABLE_DIFF_UPDATE
	OLED_NeedFullRefresh = 1;
#endif
	OLED_Clear();  // 清空显存数组
	OLED_Update(); // 更新显示，清屏，防止初始化后未显示内容时花屏
}

/**
 * 函数：OLED设置显示光标位置
 * 参数：Page 指定光标所在的页，范围0~7
 * 参数：X 指定光标所在的X轴坐标，范围0~127
 * 返回值：无
 * 说明：OLED默认的Y轴，只能8个Bit为一组写入，页等个Y轴坐
 */
void OLED_SetCursor(uint8_t Page, uint8_t X)
{
	uint8_t Command[3];

	/*如果使用此程序驱.3寸的OLED显示屏，则需要解除此注释*/
	/*因为1.3寸的OLED驱动芯片（SH1106）有132*/
	/*屏幕的起始列接在了第2列，而不是第0*/
	/*所以需要将X，才能正常显*/
	//	X += 2;

	Command[0] = 0xB0 | Page;
	Command[1] = 0x10 | ((X & 0xF0) >> 4);
	Command[2] = 0x00 | (X & 0x0F);
#if OLED_USE_HARDWARE_I2C
	OLED_WriteCommandList(Command, 3);
#else
	OLED_WriteCommand(Command[0]);
	OLED_WriteCommand(Command[1]);
	OLED_WriteCommand(Command[2]);
#endif
}

/*********************硬件配置*/

/*工具函数*********************/

/*工具函数仅供内部部分函数使用*/

/**
 * 函数：次方函数
 * 参数：X 底数
 * 参数：Y 指数
 * 返回值：无等于X的Y次方
 */
uint32_t OLED_Pow(uint32_t X, uint32_t Y)
{
	uint32_t Result = 1; // 结果默认
	while (Y--)			 // 累乘Y
	{
		Result *= X; // 每次把X累乘到结果上
	}
	return Result;
}

/**
 * 函数：判断指定点是否在指定多边形内
 * 参数：nvert 多边形的顶点
 * 参数：vertx verty 包含多边形顶点的x和y坐标的数
 * 参数：testx testy 测试点的X和y坐标
 * 返回值：无指定点是否在指定多边形内部，1：在内部：不在内
 */
uint8_t OLED_pnpoly(uint8_t nvert, int16_t *vertx, int16_t *verty, int16_t testx, int16_t testy)
{
	int16_t i, j, c = 0;

	/*此算法由W. Randolph Franklin提出*/
	/*参考链接：https://wrfranklin.org/Research/Short_Notes/pnpoly.html*/
	for (i = 0, j = nvert - 1; i < nvert; j = i++)
	{
		if (((verty[i] > testy) != (verty[j] > testy)) &&
			(testx < (vertx[j] - vertx[i]) * (testy - verty[i]) / (verty[j] - verty[i]) + vertx[i]))
		{
			c = !c;
		}
	}
	return c;
}

/**
 * 函数：判断指定点是否在指定角度内部
 * 参数：X Y 指定点的坐标
 * 参数：StartAngle EndAngle 起始角度和终止角度，范围180~180
 *           水平向右度，水平向左180度或-180度，下方为正数，上方为负数，顺时针旋
 * 返回值：无指定点是否在指定角度内部：在内部：不在内
 */
uint8_t OLED_IsInAngle(int16_t X, int16_t Y, int16_t StartAngle, int16_t EndAngle)
{
	int16_t PointAngle;
	PointAngle = atan2(Y, X) / 3.14 * 180; // 计算指定点的弧度，并转换为角度表
	if (StartAngle < EndAngle)			   // 起始角度小于终止角度的情
	{
		/*如果指定角度在起始终止角度之间，则判定指定点在指定角*/
		if (PointAngle >= StartAngle && PointAngle <= EndAngle)
		{
			return 1;
		}
	}
	else // 起始角度大于于终止角度的情况
	{
		/*如果指定角度大于起始角度或者小于终止角度，则判定指定点在指定角*/
		if (PointAngle >= StartAngle || PointAngle <= EndAngle)
		{
			return 1;
		}
	}
	return 0; // 不满足以上条件，则判断判定指定点不在指定角度
}

/*********************工具函数*/

/*功能函数*********************/

/**
 * 函数：将OLED显存数组更新到OLED屏幕
 * 参数：
 * 返回值：无
 * 说明：所有的显示函数，都只是对OLED显存数组进行读写
 *           随后调用OLED_Update函数或OLED_UpdateArea函数
 *           才会将显存数组的数据发送到OLED硬件，进行显示
 *           故调用显示函数后，要想真正地呈现在屏幕上，还需调用更新函数
 */
#if OLED_ENABLE_DIFF_UPDATE
static void OLED_UpdatePageRange(uint8_t Page, uint8_t XStart, uint8_t XEnd)
{
    uint16_t X;
    uint16_t StartX;
    uint16_t EndX;

    X = XStart;
    EndX = XEnd;
    while (X <= EndX)
    {
        if ((OLED_NeedFullRefresh != 0U) || (OLED_DisplayBuf[Page][X] != OLED_LastDisplayBuf[Page][X]))
        {
            StartX = X;
            do
            {
                X++;
            } while ((X <= EndX) && ((OLED_NeedFullRefresh != 0U) || (OLED_DisplayBuf[Page][X] != OLED_LastDisplayBuf[Page][X])));

            OLED_SetCursor(Page, (uint8_t)StartX);
            OLED_WriteData(&OLED_DisplayBuf[Page][StartX], (uint8_t)(X - StartX));
            memcpy(&OLED_LastDisplayBuf[Page][StartX], &OLED_DisplayBuf[Page][StartX], X - StartX);
        }
        else
        {
            X++;
        }
    }
}
#endif

void OLED_Update(void)
{
#if OLED_ENABLE_DIFF_UPDATE
    uint8_t j;
    for (j = 0; j < 8; j++)
    {
        OLED_UpdatePageRange(j, 0, 127);
    }
    OLED_NeedFullRefresh = 0;
#else
	uint8_t j;
	/*遍历每一*/
	for (j = 0; j < 8; j++)
	{
		/*设置光标位置为每一页的第一*/
		OLED_SetCursor(j, 0);
		/*连续写入128个数据，将显存数组的数据写入到OLED硬件*/
		OLED_WriteData(OLED_DisplayBuf[j], 128);
	}
#endif
}

/**
 * 函数：将OLED显存数组部分更新到OLED屏幕
 * 参数：X 指定区域左上角的横坐标，范围32768~32767，屏幕区域：0~127
 * 参数：Y 指定区域左上角的纵坐标，范围32768~32767，屏幕区域：0~63
 * 参数：Width 指定区域的宽度，范围0~128
 * 参数：Height 指定区域的高度，范围0~64
 * 返回值：无
 * 说明：此函数会至少更新参数指定的区
 *           如果更新区域Y轴只包含部分页，则同一页的剩余部分会跟随一起更
 * 说明：所有的显示函数，都只是对OLED显存数组进行读写
 *           随后调用OLED_Update函数或OLED_UpdateArea函数
 *           才会将显存数组的数据发送到OLED硬件，进行显示
 *           故调用显示函数后，要想真正地呈现在屏幕上，还需调用更新函数
 */
void OLED_UpdateArea(int16_t X, int16_t Y, uint8_t Width, uint8_t Height)
{
#if OLED_ENABLE_DIFF_UPDATE
    int16_t XStart;
    int16_t XEnd;
    int16_t YStart;
    int16_t YEnd;
    int16_t PageStart;
    int16_t PageEnd;
    int16_t Page;

    if ((Width == 0U) || (Height == 0U))
    {
        return;
    }

    XStart = X;
    XEnd = X + (int16_t)Width - 1;
    YStart = Y;
    YEnd = Y + (int16_t)Height - 1;

    if ((XEnd < 0) || (YEnd < 0) || (XStart > 127) || (YStart > 63))
    {
        return;
    }

    if (XStart < 0)
    {
        XStart = 0;
    }
    if (YStart < 0)
    {
        YStart = 0;
    }
    if (XEnd > 127)
    {
        XEnd = 127;
    }
    if (YEnd > 63)
    {
        YEnd = 63;
    }

    PageStart = YStart / 8;
    PageEnd = YEnd / 8;

    for (Page = PageStart; Page <= PageEnd; Page++)
    {
        OLED_UpdatePageRange((uint8_t)Page, (uint8_t)XStart, (uint8_t)XEnd);
    }
#else
	int16_t j;
	int16_t Page, Page1;

	/*负数坐标在计算页地址时需要加一个偏*/
	/*(Y + Height - 1) / 8 + 1的目的是(Y + Height) / 8并向上取*/
	Page = Y / 8;
	Page1 = (Y + Height - 1) / 8 + 1;
	if (Y < 0)
	{
		Page -= 1;
		Page1 -= 1;
	}

	/*遍历指定区域涉及的相关页*/
	for (j = Page; j < Page1; j++)
	{
		if (X >= 0 && X <= 127 && j >= 0 && j <= 7) // 超出屏幕的内容不显示
		{
			/*设置光标位置为相关页的指定列*/
			OLED_SetCursor(j, X);
			/*连续写入Width个数据，将显存数组的数据写入到OLED硬件*/
			OLED_WriteData(&OLED_DisplayBuf[j][X], Width);
		}
	}
#endif
}

/**
 * 函数：将OLED显存数组全部清零
 * 参数：
 * 返回值：无
 * 说明：调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
 */
void OLED_Clear(void)
{
	memset(OLED_DisplayBuf, 0x00, sizeof(OLED_DisplayBuf));
}

/**
 * 函数：将OLED显存数组部分清零
 * 参数：X 指定区域左上角的横坐标，范围32768~32767，屏幕区域：0~127
 * 参数：Y 指定区域左上角的纵坐标，范围32768~32767，屏幕区域：0~63
 * 参数：Width 指定区域的宽度，范围0~128
 * 参数：Height 指定区域的高度，范围0~64
 * 返回值：无
 * 说明：调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
 */
void OLED_ClearArea(int16_t X, int16_t Y, uint8_t Width, uint8_t Height)
{
	int16_t i, j;

	for (j = Y; j < Y + Height; j++) // 遍历指定
	{
		for (i = X; i < X + Width; i++) // 遍历指定
		{
			if (i >= 0 && i <= 127 && j >= 0 && j <= 63) // 超出屏幕的内容不显示
			{
				OLED_DisplayBuf[j / 8][i] &= ~(0x01 << (j % 8)); // 将显存数组指定数据清
			}
		}
	}
}

/**
 * 函数：将OLED显存数组全部取反
 * 参数：
 * 返回值：无
 * 说明：调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
 */
void OLED_Reverse(void)
{
	uint8_t i, j;
	for (j = 0; j < 8; j++) // 遍历8
	{
		for (i = 0; i < 128; i++) // 遍历128
		{
			OLED_DisplayBuf[j][i] ^= 0xFF; // 将显存数组数据全部取
		}
	}
}

/**
 * 函数：将OLED显存数组部分取反
 * 参数：X 指定区域左上角的横坐标，范围32768~32767，屏幕区域：0~127
 * 参数：Y 指定区域左上角的纵坐标，范围32768~32767，屏幕区域：0~63
 * 参数：Width 指定区域的宽度，范围0~128
 * 参数：Height 指定区域的高度，范围0~64
 * 返回值：无
 * 说明：调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
 */
void OLED_ReverseArea(int16_t X, int16_t Y, uint8_t Width, uint8_t Height)
{
	int16_t i, j;

	for (j = Y; j < Y + Height; j++) // 遍历指定
	{
		for (i = X; i < X + Width; i++) // 遍历指定
		{
			if (i >= 0 && i <= 127 && j >= 0 && j <= 63) // 超出屏幕的内容不显示
			{
				OLED_DisplayBuf[j / 8][i] ^= 0x01 << (j % 8); // 将显存数组指定数据取
			}
		}
	}
}

/**
 * 函数：OLED显示一个字
 * 参数：X 指定字符左上角的横坐标，范围32768~32767，屏幕区域：0~127
 * 参数：Y 指定字符左上角的纵坐标，范围32768~32767，屏幕区域：0~63
 * 参数：Char 指定要显示的字符，范围：ASCII码可见字
 * 参数：FontSize 指定字体大小
 *           范围：OLED_8X16		像素，高16像素
 *                 OLED_6X8		像素，高8像素
 * 返回值：无
 * 说明：调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
 */
void OLED_ShowChar(int16_t X, int16_t Y, char Char, uint8_t FontSize)
{
	if (FontSize == OLED_8X16) // 字体为宽8像素，高16像素
	{
		/*将ASCII字模库OLED_F8x16的指定数据以8*16的图像格式显*/
		OLED_ShowImage(X, Y, 8, 16, OLED_F8x16[Char - ' ']);
	}
	else if (FontSize == OLED_6X8) // 字体为宽6像素，高8像素
	{
		/*将ASCII字模库OLED_F6x8的指定数据以6*8的图像格式显*/
		OLED_ShowImage(X, Y, 6, 8, OLED_F6x8[Char - ' ']);
	}
}

/**
 * 函数：OLED显示字符串（支持ASCII码和中文混合写入
 * 参数：X 指定字符串左上角的横坐标，范围：-32768~32767，屏幕区域：0~127
 * 参数：Y 指定字符串左上角的纵坐标，范围：-32768~32767，屏幕区域：0~63
 * 参数：String 指定要显示的字符串，范围：ASCII码可见字符或中文字符组成的字符串
 * 参数：FontSize 指定字体大小
 *           范围：OLED_8X16		像素，高16像素
 *                 OLED_6X8		像素，高8像素
 * 返回值：无
 * 说明：显示的中文字符需要在OLED_Data.c里的OLED_CF16x16数组定义
 *           未找到指定中文字符时，会显示默认图形（一个方框，内部一个问号）
 *           当字体大小为OLED_8X16时，中文字符16*16点阵正常显示
 *           当字体大小为OLED_6X8时，中文字符8*8点阵显示'?'
 * 说明：调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
 */
void OLED_ShowString(int16_t X, int16_t Y, char *String, uint8_t FontSize)
{
	uint16_t i = 0;
	char SingleChar[5];
	uint8_t CharLength = 0;
	uint16_t XOffset = 0;
	uint16_t pIndex;

	while (String[i] != '\0') // 遍历字符
	{

#ifdef OLED_CHARSET_UTF8 // 定义字符集为UTF8
		/*此段代码的目的是，提取UTF8字符串中的一个字符，转存到SingleChar子字符串*/
		/*判断UTF8编码第一个字节的标志*/
		if ((String[i] & 0x80) == 0x00) // 第一个字节为0xxxxxxx
		{
			CharLength = 1;				 // 字符字节
			SingleChar[0] = String[i++]; // 将第一个字节写入SingleChar个位置，随后i指向下一个字
			SingleChar[1] = '\0';		 // 为SingleChar添加字符串结束标志位
		}
		else if ((String[i] & 0xE0) == 0xC0) // 第一个字节为110xxxxx
		{
			CharLength = 2;				 // 字符字节
			SingleChar[0] = String[i++]; // 将第一个字节写入SingleChar个位置，随后i指向下一个字
			if (String[i] == '\0')
			{
				break;
			} // 意外情况，跳出循环，结束显示
			SingleChar[1] = String[i++]; // 将第二个字节写入SingleChar个位置，随后i指向下一个字
			SingleChar[2] = '\0';		 // 为SingleChar添加字符串结束标志位
		}
		else if ((String[i] & 0xF0) == 0xE0) // 第一个字节为1110xxxx
		{
			CharLength = 3; // 字符字节
			SingleChar[0] = String[i++];
			if (String[i] == '\0')
			{
				break;
			}
			SingleChar[1] = String[i++];
			if (String[i] == '\0')
			{
				break;
			}
			SingleChar[2] = String[i++];
			SingleChar[3] = '\0';
		}
		else if ((String[i] & 0xF8) == 0xF0) // 第一个字节为11110xxx
		{
			CharLength = 4; // 字符字节
			SingleChar[0] = String[i++];
			if (String[i] == '\0')
			{
				break;
			}
			SingleChar[1] = String[i++];
			if (String[i] == '\0')
			{
				break;
			}
			SingleChar[2] = String[i++];
			if (String[i] == '\0')
			{
				break;
			}
			SingleChar[3] = String[i++];
			SingleChar[4] = '\0';
		}
		else
		{
			i++; // 意外情况，i指向下一个字节，忽略此字节，继续判断下一个字
			continue;
		}
#endif

#ifdef OLED_CHARSET_GB2312 // 定义字符集为GB2312
		/*此段代码的目的是，提取GB2312字符串中的一个字符，转存到SingleChar子字符串*/
		/*判断GB2312字节的最高位标志*/
		if ((String[i] & 0x80) == 0x00) // 最高位
		{
			CharLength = 1;				 // 字符字节
			SingleChar[0] = String[i++]; // 将第一个字节写入SingleChar个位置，随后i指向下一个字
			SingleChar[1] = '\0';		 // 为SingleChar添加字符串结束标志位
		}
		else // 最高位
		{
			CharLength = 2;				 // 字符字节
			SingleChar[0] = String[i++]; // 将第一个字节写入SingleChar个位置，随后i指向下一个字
			if (String[i] == '\0')
			{
				break;
			} // 意外情况，跳出循环，结束显示
			SingleChar[1] = String[i++]; // 将第二个字节写入SingleChar个位置，随后i指向下一个字
			SingleChar[2] = '\0';		 // 为SingleChar添加字符串结束标志位
		}
#endif

		/*显示上述代码提取到的SingleChar*/
		if (CharLength == 1) // 如果是单字节字符
		{
			/*使用OLED_ShowChar显示此字*/
			OLED_ShowChar(X + XOffset, Y, SingleChar[0], FontSize);
			XOffset += FontSize;
		}
		else // 否则，即多字节字
		{
			/*遍历整个字模库，从字模库中寻找此字符的数*/
			/*如果找到最后一个字符（定义为空字符串），则表示字符未在字模库定义，停止寻找*/
			for (pIndex = 0; strcmp(OLED_CF16x16[pIndex].Index, "") != 0; pIndex++)
			{
				/*找到匹配的字*/
				if (strcmp(OLED_CF16x16[pIndex].Index, SingleChar) == 0)
				{
					break; // 跳出循环，此时pIndex的值为指定字符的索
				}
			}
			if (FontSize == OLED_8X16) // 给定字体*16点阵
			{
				/*将字模库OLED_CF16x16的指定数据以16*16的图像格式显*/
				OLED_ShowImage(X + XOffset, Y, 16, 16, OLED_CF16x16[pIndex].Data);
				XOffset += 16;
			}
			else if (FontSize == OLED_6X8) // 给定字体8*8点阵
			{
				/*空间不足，此位置显示'?'*/
				OLED_ShowChar(X + XOffset, Y, '?', OLED_6X8);
				XOffset += OLED_6X8;
			}
		}
	}
}

/**
 * 函数：OLED显示数字（十进制，正整数
 * 参数：X 指定数字左上角的横坐标，范围32768~32767，屏幕区域：0~127
 * 参数：Y 指定数字左上角的纵坐标，范围32768~32767，屏幕区域：0~63
 * 参数：Number 指定要显示的数字，范围：0~4294967295
 * 参数：Length 指定数字的长度，范围0~10
 * 参数：FontSize 指定字体大小
 *           范围：OLED_8X16		像素，高16像素
 *                 OLED_6X8		像素，高8像素
 * 返回值：无
 * 说明：调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
 */
void OLED_ShowNum(int16_t X, int16_t Y, uint32_t Number, uint8_t Length, uint8_t FontSize)
{
	uint8_t i;
	for (i = 0; i < Length; i++) // 遍历数字的每一
	{
		/*调用OLED_ShowChar函数，依次显示每个数*/
		/*Number / OLED_Pow(10, Length - i - 1) % 10 可以十进制提取数字的每一*/
		/*+ '0' 可将数字转换为字符格*/
		OLED_ShowChar(X + i * FontSize, Y, Number / OLED_Pow(10, Length - i - 1) % 10 + '0', FontSize);
	}
}

/**
 * 函数：OLED显示有符号数字（十进制，整数
 * 参数：X 指定数字左上角的横坐标，范围32768~32767，屏幕区域：0~127
 * 参数：Y 指定数字左上角的纵坐标，范围32768~32767，屏幕区域：0~63
 * 参数：Number 指定要显示的数字，范围：-2147483648~2147483647
 * 参数：Length 指定数字的长度，范围0~10
 * 参数：FontSize 指定字体大小
 *           范围：OLED_8X16		像素，高16像素
 *                 OLED_6X8		像素，高8像素
 * 返回值：无
 * 说明：调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
 */
void OLED_ShowSignedNum(int16_t X, int16_t Y, int32_t Number, uint8_t Length, uint8_t FontSize)
{
	uint8_t i;
	uint32_t Number1;

	if (Number >= 0) // 数字大于等于0
	{
		OLED_ShowChar(X, Y, '+', FontSize); // 显示+
		Number1 = Number;					// Number1直接等于Number
	}
	else // 数字小于0
	{
		OLED_ShowChar(X, Y, '-', FontSize); // 显示-
		Number1 = -Number;					// Number1等于Number取负
	}

	for (i = 0; i < Length; i++) // 遍历数字的每一
	{
		/*调用OLED_ShowChar函数，依次显示每个数*/
		/*Number1 / OLED_Pow(10, Length - i - 1) % 10 可以十进制提取数字的每一*/
		/*+ '0' 可将数字转换为字符格*/
		OLED_ShowChar(X + (i + 1) * FontSize, Y, Number1 / OLED_Pow(10, Length - i - 1) % 10 + '0', FontSize);
	}
}

/**
 * 函数：OLED显示十六进制数字（十六进制，正整数）
 * 参数：X 指定数字左上角的横坐标，范围32768~32767，屏幕区域：0~127
 * 参数：Y 指定数字左上角的纵坐标，范围32768~32767，屏幕区域：0~63
 * 参数：Number 指定要显示的数字，范围：0x00000000~0xFFFFFFFF
 * 参数：Length 指定数字的长度，范围0~8
 * 参数：FontSize 指定字体大小
 *           范围：OLED_8X16		像素，高16像素
 *                 OLED_6X8		像素，高8像素
 * 返回值：无
 * 说明：调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
 */
void OLED_ShowHexNum(int16_t X, int16_t Y, uint32_t Number, uint8_t Length, uint8_t FontSize)
{
	uint8_t i, SingleNumber;
	for (i = 0; i < Length; i++) // 遍历数字的每一
	{
		/*以十六进制提取数字的每一*/
		SingleNumber = Number / OLED_Pow(16, Length - i - 1) % 16;

		if (SingleNumber < 10) // 单个数字小于10
		{
			/*调用OLED_ShowChar函数，显示此数字*/
			/*+ '0' 可将数字转换为字符格*/
			OLED_ShowChar(X + i * FontSize, Y, SingleNumber + '0', FontSize);
		}
		else // 单个数字大于10
		{
			/*调用OLED_ShowChar函数，显示此数字*/
			/*+ 'A' 可将数字转换为从A开始的十六进制字符*/
			OLED_ShowChar(X + i * FontSize, Y, SingleNumber - 10 + 'A', FontSize);
		}
	}
}

/**
 * 函数：OLED显示二进制数字（二进制，正整数）
 * 参数：X 指定数字左上角的横坐标，范围32768~32767，屏幕区域：0~127
 * 参数：Y 指定数字左上角的纵坐标，范围32768~32767，屏幕区域：0~63
 * 参数：Number 指定要显示的数字，范围：0x00000000~0xFFFFFFFF
 * 参数：Length 指定数字的长度，范围0~16
 * 参数：FontSize 指定字体大小
 *           范围：OLED_8X16		像素，高16像素
 *                 OLED_6X8		像素，高8像素
 * 返回值：无
 * 说明：调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
 */
void OLED_ShowBinNum(int16_t X, int16_t Y, uint32_t Number, uint8_t Length, uint8_t FontSize)
{
	uint8_t i;
	for (i = 0; i < Length; i++) // 遍历数字的每一
	{
		/*调用OLED_ShowChar函数，依次显示每个数*/
		/*Number / OLED_Pow(2, Length - i - 1) % 2 可以二进制提取数字的每一*/
		/*+ '0' 可将数字转换为字符格*/
		OLED_ShowChar(X + i * FontSize, Y, Number / OLED_Pow(2, Length - i - 1) % 2 + '0', FontSize);
	}
}

/**
 * 函数：OLED显示浮点数字（十进制，小数）
 * 参数：X 指定数字左上角的横坐标，范围32768~32767，屏幕区域：0~127
 * 参数：Y 指定数字左上角的纵坐标，范围32768~32767，屏幕区域：0~63
 * 参数：Number 指定要显示的数字，范围：-4294967295.0~4294967295.0
 * 参数：IntLength 指定数字的整数位长度，范围：0~10
 * 参数：FraLength 指定数字的小数位长度，范围：0~9，小数进行四舍五入显
 * 参数：FontSize 指定字体大小
 *           范围：OLED_8X16		像素，高16像素
 *                 OLED_6X8		像素，高8像素
 * 返回值：无
 * 说明：调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
 */
void OLED_ShowFloatNum(int16_t X, int16_t Y, double Number, uint8_t IntLength, uint8_t FraLength, uint8_t FontSize)
{
	uint32_t PowNum, IntNum, FraNum;

	if (Number >= 0) // 数字大于等于0
	{
		OLED_ShowChar(X, Y, '+', FontSize); // 显示+
	}
	else // 数字小于0
	{
		OLED_ShowChar(X, Y, '-', FontSize); // 显示-
		Number = -Number;					// Number取负
	}

	/*提取整数部分和小数部*/
	IntNum = Number;				  // 直接赋值给整型变量，提取整
	Number -= IntNum;				  // 将Number的整数减掉，防止之后将小数乘到整数时因数过大造成错误
	PowNum = OLED_Pow(10, FraLength); // 根据指定小数的位数，确定乘数
	FraNum = round(Number * PowNum);  // 将小数乘到整数，同时四舍五入，避免显示误
	IntNum += FraNum / PowNum;		  // 若四舍五入造成了进位，则需要再加给整数

	/*显示整数部分*/
	OLED_ShowNum(X + FontSize, Y, IntNum, IntLength, FontSize);

	/*显示小数*/
	OLED_ShowChar(X + (IntLength + 1) * FontSize, Y, '.', FontSize);

	/*显示小数部分*/
	OLED_ShowNum(X + (IntLength + 2) * FontSize, Y, FraNum, FraLength, FontSize);
}

/**
 * 函数：OLED显示图像
 * 参数：X 指定图像左上角的横坐标，范围32768~32767，屏幕区域：0~127
 * 参数：Y 指定图像左上角的纵坐标，范围32768~32767，屏幕区域：0~63
 * 参数：Width 指定图像的宽度，范围0~128
 * 参数：Height 指定图像的高度，范围0~64
 * 参数：Image 指定要显示的图像
 * 返回值：无
 * 说明：调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
 */
void OLED_ShowImage(int16_t X, int16_t Y, uint8_t Width, uint8_t Height, const uint8_t *Image)
{
	uint8_t i = 0, j = 0;
	int16_t Page, Shift;

	/*将图像所在区域清*/
	OLED_ClearArea(X, Y, Width, Height);

	/*遍历指定图像涉及的相关页*/
	/*(Height - 1) / 8 + 1的目的是Height / 8并向上取*/
	for (j = 0; j < (Height - 1) / 8 + 1; j++)
	{
		/*遍历指定图像涉及的相关列*/
		for (i = 0; i < Width; i++)
		{
			if (X + i >= 0 && X + i <= 127) // 超出屏幕的内容不显示
			{
				/*负数坐标在计算页地址和移位时需要加一个偏*/
				Page = Y / 8;
				Shift = Y % 8;
				if (Y < 0)
				{
					Page -= 1;
					Shift += 8;
				}

				if (Page + j >= 0 && Page + j <= 7) // 超出屏幕的内容不显示
				{
					/*显示图像在当前页的内*/
					OLED_DisplayBuf[Page + j][X + i] |= Image[j * Width + i] << (Shift);
				}

				if (Page + j + 1 >= 0 && Page + j + 1 <= 7) // 超出屏幕的内容不显示
				{
					/*显示图像在下一页的内容*/
					OLED_DisplayBuf[Page + j + 1][X + i] |= Image[j * Width + i] >> (8 - Shift);
				}
			}
		}
	}
}

/**
 * 函数：OLED使用printf函数打印格式化字符串（支持ASCII码和中文混合写入
 * 参数：X 指定格式化字符串左上角的横坐标，范围32768~32767，屏幕区域：0~127
 * 参数：Y 指定格式化字符串左上角的纵坐标，范围32768~32767，屏幕区域：0~63
 * 参数：FontSize 指定字体大小
 *           范围：OLED_8X16		像素，高16像素
 *                 OLED_6X8		像素，高8像素
 * 参数：format 指定要显示的格式化字符串，范围：ASCII码可见字符或中文字符组成的字符串
 * 参数：... 格式化字符串参数列表
 * 返回值：无
 * 说明：显示的中文字符需要在OLED_Data.c里的OLED_CF16x16数组定义
 *           未找到指定中文字符时，会显示默认图形（一个方框，内部一个问号）
 *           当字体大小为OLED_8X16时，中文字符16*16点阵正常显示
 *           当字体大小为OLED_6X8时，中文字符8*8点阵显示'?'
 * 说明：调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
 */
void OLED_Printf(int16_t X, int16_t Y, uint8_t FontSize, char *format, ...)
{
	char String[256];						 // 定义字符数组
	va_list arg;							 // 定义可变参数列表数据类型的变量arg
	va_start(arg, format);					 // 从format开始，接收参数列表到arg变量
	vsprintf(String, format, arg);			 // 使用vsprintf打印格式化字符串和参数列表到字符数组
	va_end(arg);							 // 结束变量arg
	OLED_ShowString(X, Y, String, FontSize); // OLED显示字符数组（字符串
}

/**
 * 函数：OLED在指定位置画一个点
 * 参数：X 指定点的横坐标，范围32768~32767，屏幕区域：0~127
 * 参数：Y 指定点的纵坐标，范围32768~32767，屏幕区域：0~63
 * 返回值：无
 * 说明：调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
 */
void OLED_DrawPoint(int16_t X, int16_t Y)
{
	if (X >= 0 && X <= 127 && Y >= 0 && Y <= 63) // 超出屏幕的内容不显示
	{
		/*将显存数组指定位置的一个Bit数据*/
		OLED_DisplayBuf[Y / 8][X] |= 0x01 << (Y % 8);
	}
}

/**
 * 函数：OLED获取指定位置点的
 * 参数：X 指定点的横坐标，范围32768~32767，屏幕区域：0~127
 * 参数：Y 指定点的纵坐标，范围32768~32767，屏幕区域：0~63
 * 返回值：无指定位置点是否处于点亮状态，1：点亮，0：熄
 */
uint8_t OLED_GetPoint(int16_t X, int16_t Y)
{
	if (X >= 0 && X <= 127 && Y >= 0 && Y <= 63) // 超出屏幕的内容不读取
	{
		/*判断指定位置的数*/
		if (OLED_DisplayBuf[Y / 8][X] & 0x01 << (Y % 8))
		{
			return 1; // ，返
		}
	}

	return 0; // 否则，返
}

/**
 * 函数：OLED画线
 * 参数：X0 指定一个端点的横坐标，范围32768~32767，屏幕区域：0~127
 * 参数：Y0 指定一个端点的纵坐标，范围32768~32767，屏幕区域：0~63
 * 参数：X1 指定另一个端点的横坐标，范围32768~32767，屏幕区域：0~127
 * 参数：Y1 指定另一个端点的纵坐标，范围32768~32767，屏幕区域：0~63
 * 返回值：无
 * 说明：调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
 */
void OLED_DrawLine(int16_t X0, int16_t Y0, int16_t X1, int16_t Y1)
{
	int16_t x, y, dx, dy, d, incrE, incrNE, temp;
	int16_t x0 = X0, y0 = Y0, x1 = X1, y1 = Y1;
	uint8_t yflag = 0, xyflag = 0;

	if (y0 == y1) // 横线单独处理
	{
		/*0号点X坐标大于1号点X坐标，则交换两点X坐标*/
		if (x0 > x1)
		{
			temp = x0;
			x0 = x1;
			x1 = temp;
		}

		/*遍历X坐标*/
		for (x = x0; x <= x1; x++)
		{
			OLED_DrawPoint(x, y0); // 依次画点
		}
	}
	else if (x0 == x1) // 竖线单独处理
	{
		/*0号点Y坐标大于1号点Y坐标，则交换两点Y坐标*/
		if (y0 > y1)
		{
			temp = y0;
			y0 = y1;
			y1 = temp;
		}

		/*遍历Y坐标*/
		for (y = y0; y <= y1; y++)
		{
			OLED_DrawPoint(x0, y); // 依次画点
		}
	}
	else // 斜线
	{
		/*使用Bresenham算法画直线，可以避免耗时的浮点运算，效率更高*/
		/*参考文档：https://www.cs.montana.edu/courses/spring2009/425/dslectures/Bresenham.pdf*/
		/*参考教程：https://www.bilibili.com/video/BV1364y1d7Lo*/

		if (x0 > x1) // 0号点X坐标大于1号点X坐标
		{
			/*交换两点坐标*/
			/*交换后不影响画线，但是画线方向由第一、二、三、四象限变为第一、四象限*/
			temp = x0;
			x0 = x1;
			x1 = temp;
			temp = y0;
			y0 = y1;
			y1 = temp;
		}

		if (y0 > y1) // 0号点Y坐标大于1号点Y坐标
		{
			/*将Y坐标取负*/
			/*取负后影响画线，但是画线方向由第一、四象限变为第一象限*/
			y0 = -y0;
			y1 = -y1;

			/*置标志位yflag，记住当前变换，在后续实际画线时，再将坐标换回来*/
			yflag = 1;
		}

		if (y1 - y0 > x1 - x0) // 画线斜率大于1
		{
			/*将X坐标与Y坐标互换*/
			/*互换后影响画线，但是画线方向由第一象限0~90度范围变为第一象限0~45度范*/
			temp = x0;
			x0 = y0;
			y0 = temp;
			temp = x1;
			x1 = y1;
			y1 = temp;

			/*置标志位xyflag，记住当前变换，在后续实际画线时，再将坐标换回来*/
			xyflag = 1;
		}

		/*以下为Bresenham算法画直*/
		/*算法要求，画线方向必须为第一象限0~45度范*/
		dx = x1 - x0;
		dy = y1 - y0;
		incrE = 2 * dy;
		incrNE = 2 * (dy - dx);
		d = 2 * dy - dx;
		x = x0;
		y = y0;

		/*画起始点，同时判断标志位，将坐标换回*/
		if (yflag && xyflag)
		{
			OLED_DrawPoint(y, -x);
		}
		else if (yflag)
		{
			OLED_DrawPoint(x, -y);
		}
		else if (xyflag)
		{
			OLED_DrawPoint(y, x);
		}
		else
		{
			OLED_DrawPoint(x, y);
		}

		while (x < x1) // 遍历X轴的每个
		{
			x++;
			if (d < 0) // 下一个点在当前点东方
			{
				d += incrE;
			}
			else // 下一个点在当前点东北
			{
				y++;
				d += incrNE;
			}

			/*画每一个点，同时判断标志位，将坐标换回*/
			if (yflag && xyflag)
			{
				OLED_DrawPoint(y, -x);
			}
			else if (yflag)
			{
				OLED_DrawPoint(x, -y);
			}
			else if (xyflag)
			{
				OLED_DrawPoint(y, x);
			}
			else
			{
				OLED_DrawPoint(x, y);
			}
		}
	}
}

/**
 * 函数：OLED矩形
 * 参数：X 指定矩形左上角的横坐标，范围32768~32767，屏幕区域：0~127
 * 参数：Y 指定矩形左上角的纵坐标，范围32768~32767，屏幕区域：0~63
 * 参数：Width 指定矩形的宽度，范围0~128
 * 参数：Height 指定矩形的高度，范围0~64
 * 参数：IsFilled 指定矩形是否填充
 *           范围：OLED_UNFILLED		不填
 *                 OLED_FILLED			填充
 * 返回值：无
 * 说明：调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
 */
void OLED_DrawRectangle(int16_t X, int16_t Y, uint8_t Width, uint8_t Height, uint8_t IsFilled)
{
	int16_t i, j;
	if (!IsFilled) // 指定矩形不填
	{
		/*遍历上下X坐标，画矩形上下两条*/
		for (i = X; i < X + Width; i++)
		{
			OLED_DrawPoint(i, Y);
			OLED_DrawPoint(i, Y + Height - 1);
		}
		/*遍历左右Y坐标，画矩形左右两条*/
		for (i = Y; i < Y + Height; i++)
		{
			OLED_DrawPoint(X, i);
			OLED_DrawPoint(X + Width - 1, i);
		}
	}
	else // 指定矩形填充
	{
		/*遍历X坐标*/
		for (i = X; i < X + Width; i++)
		{
			/*遍历Y坐标*/
			for (j = Y; j < Y + Height; j++)
			{
				/*在指定区域画点，填充满矩*/
				OLED_DrawPoint(i, j);
			}
		}
	}
}

/**
 * 函数：OLED三角
 * 参数：X0 指定第一个端点的横坐标，范围32768~32767，屏幕区域：0~127
 * 参数：Y0 指定第一个端点的纵坐标，范围32768~32767，屏幕区域：0~63
 * 参数：X1 指定第二个端点的横坐标，范围32768~32767，屏幕区域：0~127
 * 参数：Y1 指定第二个端点的纵坐标，范围32768~32767，屏幕区域：0~63
 * 参数：X2 指定第三个端点的横坐标，范围32768~32767，屏幕区域：0~127
 * 参数：Y2 指定第三个端点的纵坐标，范围32768~32767，屏幕区域：0~63
 * 参数：IsFilled 指定三角形是否填
 *           范围：OLED_UNFILLED		不填
 *                 OLED_FILLED			填充
 * 返回值：无
 * 说明：调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
 */
void OLED_DrawTriangle(int16_t X0, int16_t Y0, int16_t X1, int16_t Y1, int16_t X2, int16_t Y2, uint8_t IsFilled)
{
	int16_t minx = X0, miny = Y0, maxx = X0, maxy = Y0;
	int16_t i, j;
	int16_t vx[] = {X0, X1, X2};
	int16_t vy[] = {Y0, Y1, Y2};

	if (!IsFilled) // 指定三角形不填充
	{
		/*调用画线函数，将三个点用直线连接*/
		OLED_DrawLine(X0, Y0, X1, Y1);
		OLED_DrawLine(X0, Y0, X2, Y2);
		OLED_DrawLine(X1, Y1, X2, Y2);
	}
	else // 指定三角形填
	{
		/*找到三个点最小的X、Y坐标*/
		if (X1 < minx)
		{
			minx = X1;
		}
		if (X2 < minx)
		{
			minx = X2;
		}
		if (Y1 < miny)
		{
			miny = Y1;
		}
		if (Y2 < miny)
		{
			miny = Y2;
		}

		/*找到三个点最大的X、Y坐标*/
		if (X1 > maxx)
		{
			maxx = X1;
		}
		if (X2 > maxx)
		{
			maxx = X2;
		}
		if (Y1 > maxy)
		{
			maxy = Y1;
		}
		if (Y2 > maxy)
		{
			maxy = Y2;
		}

		/*最小最大坐标之间的矩形为可能需要填充的区域*/
		/*遍历此区域中所有的*/
		/*遍历X坐标*/
		for (i = minx; i <= maxx; i++)
		{
			/*遍历Y坐标*/
			for (j = miny; j <= maxy; j++)
			{
				/*调用OLED_pnpoly，判断指定点是否在指定三角形之中*/
				/*如果在，则画点，如果不在，则不做处理*/
				if (OLED_pnpoly(3, vx, vy, i, j))
				{
					OLED_DrawPoint(i, j);
				}
			}
		}
	}
}

/**
 * 函数：OLED画圆
 * 参数：X 指定圆的圆心横坐标，范围32768~32767，屏幕区域：0~127
 * 参数：Y 指定圆的圆心纵坐标，范围32768~32767，屏幕区域：0~63
 * 参数：Radius 指定圆的半径，范围：0~255
 * 参数：IsFilled 指定圆是否填
 *           范围：OLED_UNFILLED		不填
 *                 OLED_FILLED			填充
 * 返回值：无
 * 说明：调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
 */
void OLED_DrawCircle(int16_t X, int16_t Y, uint8_t Radius, uint8_t IsFilled)
{
	int16_t x, y, d, j;

	/*使用Bresenham算法画圆，可以避免耗时的浮点运算，效率更高*/
	/*参考文档：https://www.cs.montana.edu/courses/spring2009/425/dslectures/Bresenham.pdf*/
	/*参考教程：https://www.bilibili.com/video/BV1VM4y1u7wJ*/

	d = 1 - Radius;
	x = 0;
	y = Radius;

	/*画每个八分之一圆弧的起始点*/
	OLED_DrawPoint(X + x, Y + y);
	OLED_DrawPoint(X - x, Y - y);
	OLED_DrawPoint(X + y, Y + x);
	OLED_DrawPoint(X - y, Y - x);

	if (IsFilled) // 指定圆填
	{
		/*遍历起始点Y坐标*/
		for (j = -y; j < y; j++)
		{
			/*在指定区域画点，填充部分*/
			OLED_DrawPoint(X, Y + j);
		}
	}

	while (x < y) // 遍历X轴的每个
	{
		x++;
		if (d < 0) // 下一个点在当前点东方
		{
			d += 2 * x + 1;
		}
		else // 下一个点在当前点东南
		{
			y--;
			d += 2 * (x - y) + 1;
		}

		/*画每个八分之一圆弧的点*/
		OLED_DrawPoint(X + x, Y + y);
		OLED_DrawPoint(X + y, Y + x);
		OLED_DrawPoint(X - x, Y - y);
		OLED_DrawPoint(X - y, Y - x);
		OLED_DrawPoint(X + x, Y - y);
		OLED_DrawPoint(X + y, Y - x);
		OLED_DrawPoint(X - x, Y + y);
		OLED_DrawPoint(X - y, Y + x);

		if (IsFilled) // 指定圆填
		{
			/*遍历中间部分*/
			for (j = -y; j < y; j++)
			{
				/*在指定区域画点，填充部分*/
				OLED_DrawPoint(X + x, Y + j);
				OLED_DrawPoint(X - x, Y + j);
			}

			/*遍历两侧部分*/
			for (j = -x; j < x; j++)
			{
				/*在指定区域画点，填充部分*/
				OLED_DrawPoint(X - y, Y + j);
				OLED_DrawPoint(X + y, Y + j);
			}
		}
	}
}

/**
 * 函数：OLED画椭
 * 参数：X 指定椭圆的圆心横坐标，范围：-32768~32767，屏幕区域：0~127
 * 参数：Y 指定椭圆的圆心纵坐标，范围：-32768~32767，屏幕区域：0~63
 * 参数：A 指定椭圆的横向半轴长度，范围0~255
 * 参数：B 指定椭圆的纵向半轴长度，范围0~255
 * 参数：IsFilled 指定椭圆是否填充
 *           范围：OLED_UNFILLED		不填
 *                 OLED_FILLED			填充
 * 返回值：无
 * 说明：调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
 */
void OLED_DrawEllipse(int16_t X, int16_t Y, uint8_t A, uint8_t B, uint8_t IsFilled)
{
	int16_t x, y, j;
	int16_t a = A, b = B;
	float d1, d2;

	/*使用Bresenham算法画椭圆，可以避免部分耗时的浮点运算，效率更高*/
	/*参考链接：https://blog.csdn.net/myf_666/article/details/128167392*/

	x = 0;
	y = b;
	d1 = b * b + a * a * (-b + 0.5);

	if (IsFilled) // 指定椭圆填充
	{
		/*遍历起始点Y坐标*/
		for (j = -y; j < y; j++)
		{
			/*在指定区域画点，填充部分椭圆*/
			OLED_DrawPoint(X, Y + j);
			OLED_DrawPoint(X, Y + j);
		}
	}

	/*画椭圆弧的起始点*/
	OLED_DrawPoint(X + x, Y + y);
	OLED_DrawPoint(X - x, Y - y);
	OLED_DrawPoint(X - x, Y + y);
	OLED_DrawPoint(X + x, Y - y);

	/*画椭圆中间部*/
	while (b * b * (x + 1) < a * a * (y - 0.5))
	{
		if (d1 <= 0) // 下一个点在当前点东方
		{
			d1 += b * b * (2 * x + 3);
		}
		else // 下一个点在当前点东南
		{
			d1 += b * b * (2 * x + 3) + a * a * (-2 * y + 2);
			y--;
		}
		x++;

		if (IsFilled) // 指定椭圆填充
		{
			/*遍历中间部分*/
			for (j = -y; j < y; j++)
			{
				/*在指定区域画点，填充部分椭圆*/
				OLED_DrawPoint(X + x, Y + j);
				OLED_DrawPoint(X - x, Y + j);
			}
		}

		/*画椭圆中间部分圆*/
		OLED_DrawPoint(X + x, Y + y);
		OLED_DrawPoint(X - x, Y - y);
		OLED_DrawPoint(X - x, Y + y);
		OLED_DrawPoint(X + x, Y - y);
	}

	/*画椭圆两侧部*/
	d2 = b * b * (x + 0.5) * (x + 0.5) + a * a * (y - 1) * (y - 1) - a * a * b * b;

	while (y > 0)
	{
		if (d2 <= 0) // 下一个点在当前点东方
		{
			d2 += b * b * (2 * x + 2) + a * a * (-2 * y + 3);
			x++;
		}
		else // 下一个点在当前点东南
		{
			d2 += a * a * (-2 * y + 3);
		}
		y--;

		if (IsFilled) // 指定椭圆填充
		{
			/*遍历两侧部分*/
			for (j = -y; j < y; j++)
			{
				/*在指定区域画点，填充部分椭圆*/
				OLED_DrawPoint(X + x, Y + j);
				OLED_DrawPoint(X - x, Y + j);
			}
		}

		/*画椭圆两侧部分圆*/
		OLED_DrawPoint(X + x, Y + y);
		OLED_DrawPoint(X - x, Y - y);
		OLED_DrawPoint(X - x, Y + y);
		OLED_DrawPoint(X + x, Y - y);
	}
}

/**
 * 函数：OLED画圆
 * 参数：X 指定圆弧的圆心横坐标，范围：-32768~32767，屏幕区域：0~127
 * 参数：Y 指定圆弧的圆心纵坐标，范围：-32768~32767，屏幕区域：0~63
 * 参数：Radius 指定圆弧的半径，范围0~255
 * 参数：StartAngle 指定圆弧的起始角度，范围180~180
 *           水平向右度，水平向左180度或-180度，下方为正数，上方为负数，顺时针旋
 * 参数：EndAngle 指定圆弧的终止角度，范围180~180
 *           水平向右度，水平向左180度或-180度，下方为正数，上方为负数，顺时针旋
 * 参数：IsFilled 指定圆弧是否填充，填充后为扇
 *           范围：OLED_UNFILLED		不填
 *                 OLED_FILLED			填充
 * 返回值：无
 * 说明：调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
 */
void OLED_DrawArc(int16_t X, int16_t Y, uint8_t Radius, int16_t StartAngle, int16_t EndAngle, uint8_t IsFilled)
{
	int16_t x, y, d, j;

	/*此函数借用Bresenham算法画圆的方*/

	d = 1 - Radius;
	x = 0;
	y = Radius;

	/*在画圆的每个点时，判断指定点是否在指定角度内，在，则画点，不在，则不做处*/
	if (OLED_IsInAngle(x, y, StartAngle, EndAngle))
	{
		OLED_DrawPoint(X + x, Y + y);
	}
	if (OLED_IsInAngle(-x, -y, StartAngle, EndAngle))
	{
		OLED_DrawPoint(X - x, Y - y);
	}
	if (OLED_IsInAngle(y, x, StartAngle, EndAngle))
	{
		OLED_DrawPoint(X + y, Y + x);
	}
	if (OLED_IsInAngle(-y, -x, StartAngle, EndAngle))
	{
		OLED_DrawPoint(X - y, Y - x);
	}

	if (IsFilled) // 指定圆弧填充
	{
		/*遍历起始点Y坐标*/
		for (j = -y; j < y; j++)
		{
			/*在填充圆的每个点时，判断指定点是否在指定角度内，在，则画点，不在，则不做处理*/
			if (OLED_IsInAngle(0, j, StartAngle, EndAngle))
			{
				OLED_DrawPoint(X, Y + j);
			}
		}
	}

	while (x < y) // 遍历X轴的每个
	{
		x++;
		if (d < 0) // 下一个点在当前点东方
		{
			d += 2 * x + 1;
		}
		else // 下一个点在当前点东南
		{
			y--;
			d += 2 * (x - y) + 1;
		}

		/*在画圆的每个点时，判断指定点是否在指定角度内，在，则画点，不在，则不做处*/
		if (OLED_IsInAngle(x, y, StartAngle, EndAngle))
		{
			OLED_DrawPoint(X + x, Y + y);
		}
		if (OLED_IsInAngle(y, x, StartAngle, EndAngle))
		{
			OLED_DrawPoint(X + y, Y + x);
		}
		if (OLED_IsInAngle(-x, -y, StartAngle, EndAngle))
		{
			OLED_DrawPoint(X - x, Y - y);
		}
		if (OLED_IsInAngle(-y, -x, StartAngle, EndAngle))
		{
			OLED_DrawPoint(X - y, Y - x);
		}
		if (OLED_IsInAngle(x, -y, StartAngle, EndAngle))
		{
			OLED_DrawPoint(X + x, Y - y);
		}
		if (OLED_IsInAngle(y, -x, StartAngle, EndAngle))
		{
			OLED_DrawPoint(X + y, Y - x);
		}
		if (OLED_IsInAngle(-x, y, StartAngle, EndAngle))
		{
			OLED_DrawPoint(X - x, Y + y);
		}
		if (OLED_IsInAngle(-y, x, StartAngle, EndAngle))
		{
			OLED_DrawPoint(X - y, Y + x);
		}

		if (IsFilled) // 指定圆弧填充
		{
			/*遍历中间部分*/
			for (j = -y; j < y; j++)
			{
				/*在填充圆的每个点时，判断指定点是否在指定角度内，在，则画点，不在，则不做处理*/
				if (OLED_IsInAngle(x, j, StartAngle, EndAngle))
				{
					OLED_DrawPoint(X + x, Y + j);
				}
				if (OLED_IsInAngle(-x, j, StartAngle, EndAngle))
				{
					OLED_DrawPoint(X - x, Y + j);
				}
			}

			/*遍历两侧部分*/
			for (j = -x; j < x; j++)
			{
				/*在填充圆的每个点时，判断指定点是否在指定角度内，在，则画点，不在，则不做处理*/
				if (OLED_IsInAngle(-y, j, StartAngle, EndAngle))
				{
					OLED_DrawPoint(X - y, Y + j);
				}
				if (OLED_IsInAngle(y, j, StartAngle, EndAngle))
				{
					OLED_DrawPoint(X + y, Y + j);
				}
			}
		}
	}
}

/*********************功能函数*/

/*****************江协科技|版权所***************/
/*****************jiangxiekeji.com*****************/





