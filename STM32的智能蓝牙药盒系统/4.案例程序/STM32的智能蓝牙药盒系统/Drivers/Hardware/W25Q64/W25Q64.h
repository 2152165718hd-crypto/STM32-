#ifndef __W25Q64_H
#define __W25Q64_H

/**
 * @file W25Q64.h
 * @brief W25Q64 外部 SPI Flash 驱动接口声明。
 */

#include "stm32f1xx_hal.h"

/* ---- 引脚定义 ---- */
#define W25Q64_PORT GPIOA
#define W25Q64_CS_PIN GPIO_PIN_4
#define W25Q64_CLK_PIN GPIO_PIN_5
#define W25Q64_MISO_PIN GPIO_PIN_6
#define W25Q64_MOSI_PIN GPIO_PIN_7

/* ---- W25Q64 指令集 ---- */
#define W25Q64_WRITE_ENABLE 0x06
#define W25Q64_WRITE_DISABLE 0x04
#define W25Q64_READ_STATUS_REG1 0x05
#define W25Q64_READ_STATUS_REG2 0x35
#define W25Q64_WRITE_STATUS_REG 0x01
#define W25Q64_READ_DATA 0x03
#define W25Q64_FAST_READ 0x0B
#define W25Q64_PAGE_PROGRAM 0x02
#define W25Q64_SECTOR_ERASE_4KB 0x20
#define W25Q64_BLOCK_ERASE_32KB 0x52
#define W25Q64_BLOCK_ERASE_64KB 0xD8
#define W25Q64_CHIP_ERASE 0xC7
#define W25Q64_POWER_DOWN 0xB9
#define W25Q64_RELEASE_POWER_DOWN 0xAB
#define W25Q64_DEVICE_ID 0xAB
#define W25Q64_MANUFACTURER_ID 0x90
#define W25Q64_JEDEC_ID 0x9F

/* ---- W25Q64 参数 ---- */
#define W25Q64_PAGE_SIZE 256
#define W25Q64_SECTOR_SIZE 4096
#define W25Q64_BLOCK_SIZE 65536
#define W25Q64_TOTAL_SIZE (8 * 1024 * 1024) /* 8MB */

/* ---- 公共函数 ---- */
/**
 * @brief 初始化 W25Q64 软件 SPI 引脚。
 */
void W25Q64_Init(void);

/**
 * @brief 读取厂商 ID 与设备 ID。
 * @return 16 位 ID 信息。
 */
uint16_t W25Q64_ReadID(void);

/**
 * @brief 读取 24 位 JEDEC ID。
 * @return JEDEC ID。
 */
uint32_t W25Q64_ReadJEDEC_ID(void);

/**
 * @brief 从指定地址读取数据。
 * @param addr 起始地址。
 * @param buf 输出缓冲区。
 * @param len 读取长度。
 */
void W25Q64_ReadData(uint32_t addr, uint8_t *buf, uint32_t len);

/**
 * @brief 在单页范围内写入数据。
 * @param addr 起始地址。
 * @param buf 输入数据。
 * @param len 写入长度，不能跨页。
 */
void W25Q64_WritePage(uint32_t addr, const uint8_t *buf, uint16_t len);

/**
 * @brief 跨页连续写入数据。
 * @param addr 起始地址。
 * @param buf 输入数据。
 * @param len 写入长度。
 */
void W25Q64_WriteData(uint32_t addr, const uint8_t *buf, uint32_t len);

/**
 * @brief 擦除所在 4KB 扇区。
 * @param addr 扇区内任意地址。
 */
void W25Q64_SectorErase(uint32_t addr);

/**
 * @brief 擦除所在 32KB 块。
 * @param addr 块内任意地址。
 */
void W25Q64_BlockErase_32KB(uint32_t addr);

/**
 * @brief 擦除所在 64KB 块。
 * @param addr 块内任意地址。
 */
void W25Q64_BlockErase_64KB(uint32_t addr);

/**
 * @brief 执行整片擦除。
 */
void W25Q64_ChipErase(void);

/**
 * @brief 使芯片进入掉电模式。
 */
void W25Q64_PowerDown(void);

/**
 * @brief 唤醒芯片退出掉电模式。
 */
void W25Q64_WakeUp(void);

/**
 * @brief 读取状态寄存器 1。
 * @return 状态寄存器 1 的值。
 */
uint8_t W25Q64_ReadStatusReg1(void);

/**
 * @brief 等待芯片退出忙状态。
 */
void W25Q64_WaitBusy(void);

#endif /* __W25Q64_H */
