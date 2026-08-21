#include ".\Hardware\W25Q64\W25Q64.h"

/**
 * @file W25Q64.c
 * @brief W25Q64 外部 SPI Flash 驱动实现。
 */

/* ========== 私有宏：GPIO 快捷操作 ========== */
#define W25Q64_CS_LOW() HAL_GPIO_WritePin(W25Q64_PORT, W25Q64_CS_PIN, GPIO_PIN_RESET)
#define W25Q64_CS_HIGH() HAL_GPIO_WritePin(W25Q64_PORT, W25Q64_CS_PIN, GPIO_PIN_SET)
#define W25Q64_CLK_LOW() HAL_GPIO_WritePin(W25Q64_PORT, W25Q64_CLK_PIN, GPIO_PIN_RESET)
#define W25Q64_CLK_HIGH() HAL_GPIO_WritePin(W25Q64_PORT, W25Q64_CLK_PIN, GPIO_PIN_SET)
#define W25Q64_MOSI_LOW() HAL_GPIO_WritePin(W25Q64_PORT, W25Q64_MOSI_PIN, GPIO_PIN_RESET)
#define W25Q64_MOSI_HIGH() HAL_GPIO_WritePin(W25Q64_PORT, W25Q64_MOSI_PIN, GPIO_PIN_SET)
#define W25Q64_MISO_READ() HAL_GPIO_ReadPin(W25Q64_PORT, W25Q64_MISO_PIN)

/* ==================================================================
 *  底层软件 SPI 通信
 * ================================================================== */

/**
 * @brief  软件 SPI 交换一个字节 (CPOL=0, CPHA=0，MSB first)
 */
static uint8_t W25Q64_SPI_SwapByte(uint8_t txData)
{
    uint8_t rxData = 0;
    uint8_t i;

    for (i = 0; i < 8; i++)
    {
        /* 发送：高位先出 */
        if (txData & 0x80)
            W25Q64_MOSI_HIGH();
        else
            W25Q64_MOSI_LOW();
        txData <<= 1;

        /* CLK 上升沿：采样 MISO */
        W25Q64_CLK_HIGH();
        rxData <<= 1;
        if (W25Q64_MISO_READ())
            rxData |= 0x01;

        /* CLK 下降沿 */
        W25Q64_CLK_LOW();
    }
    return rxData;
}

/* ==================================================================
 *  内部辅助函数
 * ================================================================== */

static void W25Q64_WriteEnable(void)
{
    W25Q64_CS_LOW();
    W25Q64_SPI_SwapByte(W25Q64_WRITE_ENABLE);
    W25Q64_CS_HIGH();
}

/* ==================================================================
 *  公共函数实现
 * ================================================================== */

/**
 * @brief  初始化 W25Q64 所用 GPIO (软件 SPI)
 */
void W25Q64_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* CS, CLK, MOSI —— 推挽输出 */
    GPIO_InitStruct.Pin = W25Q64_CS_PIN | W25Q64_CLK_PIN | W25Q64_MOSI_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(W25Q64_PORT, &GPIO_InitStruct);

    /* MISO —— 上拉输入 */
    GPIO_InitStruct.Pin = W25Q64_MISO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(W25Q64_PORT, &GPIO_InitStruct);

    /* 默认状态：CS 高，CLK 低 */
    W25Q64_CS_HIGH();
    W25Q64_CLK_LOW();
}

/**
 * @brief  读取厂商 ID 和设备 ID
 * @retval 高 8 位 = 厂商 ID (EFh)，低 8 位 = 设备 ID (16h)
 */
uint16_t W25Q64_ReadID(void)
{
    uint16_t id;

    W25Q64_CS_LOW();
    W25Q64_SPI_SwapByte(W25Q64_MANUFACTURER_ID);
    W25Q64_SPI_SwapByte(0x00);
    W25Q64_SPI_SwapByte(0x00);
    W25Q64_SPI_SwapByte(0x00);
    id = (uint16_t)W25Q64_SPI_SwapByte(0xFF) << 8; /* 厂商 ID */
    id |= W25Q64_SPI_SwapByte(0xFF);               /* 设备 ID */
    W25Q64_CS_HIGH();

    return id;
}

/**
 * @brief  读取 JEDEC ID (厂商 + 存储类型 + 容量)
 * @retval 24 位 JEDEC ID，W25Q64 应返回 0xEF4017
 */
uint32_t W25Q64_ReadJEDEC_ID(void)
{
    uint32_t id;

    W25Q64_CS_LOW();
    W25Q64_SPI_SwapByte(W25Q64_JEDEC_ID);
    id = (uint32_t)W25Q64_SPI_SwapByte(0xFF) << 16;
    id |= (uint32_t)W25Q64_SPI_SwapByte(0xFF) << 8;
    id |= W25Q64_SPI_SwapByte(0xFF);
    W25Q64_CS_HIGH();

    return id;
}

/**
 * @brief  读取状态寄存器 1
 * @retval 状态寄存器 1 值 (bit0 = BUSY)
 */
uint8_t W25Q64_ReadStatusReg1(void)
{
    uint8_t status;

    W25Q64_CS_LOW();
    W25Q64_SPI_SwapByte(W25Q64_READ_STATUS_REG1);
    status = W25Q64_SPI_SwapByte(0xFF);
    W25Q64_CS_HIGH();

    return status;
}

/**
 * @brief  等待 W25Q64 内部操作完成 (BUSY 位清零)
 */
void W25Q64_WaitBusy(void)
{
    while (W25Q64_ReadStatusReg1() & 0x01)
    {
        /* 等待 BUSY 位清零 */
    }
}

/**
 * @brief  从指定地址读取数据
 * @param  addr  起始地址 (24 位)
 * @param  buf   目标缓冲区
 * @param  len   读取长度 (字节)
 */
void W25Q64_ReadData(uint32_t addr, uint8_t *buf, uint32_t len)
{
    uint32_t i;

    W25Q64_WaitBusy();
    W25Q64_CS_LOW();
    W25Q64_SPI_SwapByte(W25Q64_READ_DATA);
    W25Q64_SPI_SwapByte((uint8_t)(addr >> 16));
    W25Q64_SPI_SwapByte((uint8_t)(addr >> 8));
    W25Q64_SPI_SwapByte((uint8_t)(addr));

    for (i = 0; i < len; i++)
    {
        buf[i] = W25Q64_SPI_SwapByte(0xFF);
    }
    W25Q64_CS_HIGH();
}

/**
 * @brief  页编程 (最多写入 256 字节，不可跨页)
 * @param  addr  起始地址 (必须页对齐或在页内)
 * @param  buf   待写入数据
 * @param  len   写入长度 (1~256)
 */
void W25Q64_WritePage(uint32_t addr, const uint8_t *buf, uint16_t len)
{
    uint16_t i;

    if (len == 0 || len > W25Q64_PAGE_SIZE)
        return;

    W25Q64_WaitBusy();
    W25Q64_WriteEnable();

    W25Q64_CS_LOW();
    W25Q64_SPI_SwapByte(W25Q64_PAGE_PROGRAM);
    W25Q64_SPI_SwapByte((uint8_t)(addr >> 16));
    W25Q64_SPI_SwapByte((uint8_t)(addr >> 8));
    W25Q64_SPI_SwapByte((uint8_t)(addr));

    for (i = 0; i < len; i++)
    {
        W25Q64_SPI_SwapByte(buf[i]);
    }
    W25Q64_CS_HIGH();

    W25Q64_WaitBusy();
}

/**
 * @brief  任意长度写入 (自动处理跨页)
 * @param  addr  起始地址
 * @param  buf   待写入数据
 * @param  len   写入长度
 */
void W25Q64_WriteData(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    uint16_t pageRemain;

    while (len > 0)
    {
        /* 当前页剩余可写字节数 */
        pageRemain = W25Q64_PAGE_SIZE - (addr % W25Q64_PAGE_SIZE);
        if (pageRemain > len)
            pageRemain = (uint16_t)len;

        W25Q64_WritePage(addr, buf, pageRemain);

        addr += pageRemain;
        buf += pageRemain;
        len -= pageRemain;
    }
}

/**
 * @brief  扇区擦除 (4KB)
 * @param  addr  扇区内任意地址
 */
void W25Q64_SectorErase(uint32_t addr)
{
    W25Q64_WaitBusy();
    W25Q64_WriteEnable();

    W25Q64_CS_LOW();
    W25Q64_SPI_SwapByte(W25Q64_SECTOR_ERASE_4KB);
    W25Q64_SPI_SwapByte((uint8_t)(addr >> 16));
    W25Q64_SPI_SwapByte((uint8_t)(addr >> 8));
    W25Q64_SPI_SwapByte((uint8_t)(addr));
    W25Q64_CS_HIGH();

    W25Q64_WaitBusy();
}

/**
 * @brief  块擦除 (32KB)
 * @param  addr  块内任意地址
 */
void W25Q64_BlockErase_32KB(uint32_t addr)
{
    W25Q64_WaitBusy();
    W25Q64_WriteEnable();

    W25Q64_CS_LOW();
    W25Q64_SPI_SwapByte(W25Q64_BLOCK_ERASE_32KB);
    W25Q64_SPI_SwapByte((uint8_t)(addr >> 16));
    W25Q64_SPI_SwapByte((uint8_t)(addr >> 8));
    W25Q64_SPI_SwapByte((uint8_t)(addr));
    W25Q64_CS_HIGH();

    W25Q64_WaitBusy();
}

/**
 * @brief  块擦除 (64KB)
 * @param  addr  块内任意地址
 */
void W25Q64_BlockErase_64KB(uint32_t addr)
{
    W25Q64_WaitBusy();
    W25Q64_WriteEnable();

    W25Q64_CS_LOW();
    W25Q64_SPI_SwapByte(W25Q64_BLOCK_ERASE_64KB);
    W25Q64_SPI_SwapByte((uint8_t)(addr >> 16));
    W25Q64_SPI_SwapByte((uint8_t)(addr >> 8));
    W25Q64_SPI_SwapByte((uint8_t)(addr));
    W25Q64_CS_HIGH();

    W25Q64_WaitBusy();
}

/**
 * @brief  全片擦除 (耗时较长，约 20~100 秒)
 */
void W25Q64_ChipErase(void)
{
    W25Q64_WaitBusy();
    W25Q64_WriteEnable();

    W25Q64_CS_LOW();
    W25Q64_SPI_SwapByte(W25Q64_CHIP_ERASE);
    W25Q64_CS_HIGH();

    W25Q64_WaitBusy();
}

/**
 * @brief  进入掉电模式 (低功耗)
 */
void W25Q64_PowerDown(void)
{
    W25Q64_CS_LOW();
    W25Q64_SPI_SwapByte(W25Q64_POWER_DOWN);
    W25Q64_CS_HIGH();
}

/**
 * @brief  唤醒 (退出掉电模式)
 */
void W25Q64_WakeUp(void)
{
    W25Q64_CS_LOW();
    W25Q64_SPI_SwapByte(W25Q64_RELEASE_POWER_DOWN);
    W25Q64_CS_HIGH();
}
