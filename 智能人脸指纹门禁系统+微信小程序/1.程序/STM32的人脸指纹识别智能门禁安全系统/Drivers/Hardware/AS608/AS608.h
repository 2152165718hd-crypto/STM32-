#ifndef __AS608_H__
#define __AS608_H__

#include "stm32f1xx_hal.h"
#include <stdint.h>

/* ==================== 引脚定义 ==================== */
#define AS608_GPIO_PORT GPIOB
#define AS608_TX_PIN GPIO_PIN_10
#define AS608_RX_PIN GPIO_PIN_11
#define AS608_WAK_PIN GPIO_PIN_1
#define AS608_UART USART3

/* ==================== 串口参数 ==================== */
#define AS608_UART_BAUD 57600U
#define AS608_CMD_TIMEOUT_MS 2000U

/* ==================== 协议常量 ==================== */
#define CharBuffer1 0x01
#define CharBuffer2 0x02

extern uint32_t AS608Addr;

typedef struct
{
	uint16_t pageID;
	uint16_t mathscore;
} SearchResult;

typedef struct
{
	uint16_t GZ_max;
	uint8_t GZ_level;
	uint32_t GZ_addr;
	uint8_t GZ_size;
	uint8_t GZ_N;
} SysPara;

/* ==================== API ==================== */

/* 初始化 USART3 与 WAK 状态引脚 */
void AS608_Init(void);

/* 仅初始化 WAK 状态引脚（兼容参考代码命名） */
void GZ_StaGPIO_Init(void);

uint8_t GZ_GetImage(void);
uint8_t GZ_GenChar(uint8_t BufferID);
uint8_t GZ_Match(void);
uint8_t GZ_Search(uint8_t BufferID, uint16_t StartPage, uint16_t PageNum, SearchResult *p);
uint8_t GZ_RegModel(void);
uint8_t GZ_StoreChar(uint8_t BufferID, uint16_t PageID);
uint8_t GZ_DeletChar(uint16_t PageID, uint16_t N);
uint8_t GZ_Empty(void);
uint8_t GZ_WriteReg(uint8_t RegNum, uint8_t DATA);
uint8_t GZ_ReadSysPara(SysPara *p);
uint8_t GZ_SetAddr(uint32_t GZ_addr);
uint8_t GZ_WriteNotepad(uint8_t NotePageNum, uint8_t *content);
uint8_t GZ_ReadNotepad(uint8_t NotePageNum, uint8_t *note);
uint8_t GZ_HighSpeedSearch(uint8_t BufferID, uint16_t StartPage, uint16_t PageNum, SearchResult *p);
uint8_t GZ_ValidTempleteNum(uint16_t *ValidN);
uint8_t GZ_HandShake(uint32_t *GZ_Addr);

/* 确认码信息解析 */
const char *EnsureMessage(uint8_t ensure);

#endif /* __AS608_H__ */
