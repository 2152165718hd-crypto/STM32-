#ifndef __BLUETOOTH_H
#define __BLUETOOTH_H

#include "stm32f1xx_hal.h"

/* ---- 引脚定义 ---- */
#define BT_GPIO_PORT GPIOA
#define BT_TX_PIN GPIO_PIN_2 /* PA2 -> 蓝牙模块 RXD */
#define BT_RX_PIN GPIO_PIN_3 /* PA3 <- 蓝牙模块 TXD */
#define BT_UART USART2

/* ---- 参数配置 ---- */
#define BT_BAUDRATE 9600     /* JDY-31 默认波特率       */
#define BT_RX_BUF_SIZE 256   /* 接收环形缓冲区大小      */
#define BT_AT_TIMEOUT_MS 500 /* AT 指令应答超时 (ms)    */
#define BT_AT_RESP_SIZE 128  /* AT 应答缓冲区大小       */

/* ---- 波特率枚举 (AT+BAUD) ---- */
typedef enum
{
    BT_BAUD_1200 = 1,
    BT_BAUD_2400 = 2,
    BT_BAUD_4800 = 3,
    BT_BAUD_9600 = 4, /* 默认 */
    BT_BAUD_19200 = 5,
    BT_BAUD_38400 = 6,
    BT_BAUD_57600 = 7,
    BT_BAUD_115200 = 8,
    BT_BAUD_128000 = 9,
} BT_Baud_t;

/* ---- 公共函数 ---- */

/**
 * @brief  初始化蓝牙模块（PA2=TX, PA3=RX, USART2）
 */
void Bluetooth_Init(void);

/**
 * @brief  发送数据（阻塞，超时 200ms）
 * @param  data  待发送数据指针
 * @param  len   数据长度
 */
void Bluetooth_Send(const uint8_t *data, uint16_t len);

/**
 * @brief  发送字符串（阻塞）
 * @param  str  以 '\0' 结尾的字符串
 */
void Bluetooth_SendString(const char *str);

/**
 * @brief  获取接收缓冲区中可读字节数
 * @retval 可读字节数
 */
uint16_t Bluetooth_Available(void);

/**
 * @brief  从接收缓冲区读取一个字节
 * @param  byte  读出的字节存放地址
 * @retval 1=读取成功, 0=缓冲区为空
 */
uint8_t Bluetooth_ReadByte(uint8_t *byte);

/**
 * @brief  从接收缓冲区读取多个字节
 * @param  buf     目标缓冲区
 * @param  maxLen  最多读取的字节数
 * @retval 实际读取的字节数
 */
uint16_t Bluetooth_Read(uint8_t *buf, uint16_t maxLen);

/**
 * @brief  清空接收缓冲区
 */
void Bluetooth_FlushRx(void);

/**
 * @brief  UART 接收完成回调，用户需在 HAL_UART_RxCpltCallback 中调用
 * @code
 *   void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
 *       if (huart->Instance == USART2) BLUETOOTH_RxCpltCallback();
 *   }
 * @endcode
 */
void BLUETOOTH_RxCpltCallback(void);

/* ---- AT 指令配置 API ---- */

/**
 * @brief  发送 AT 指令并等待应答
 * @param  cmd      完整 AT 指令字符串，如 "AT+VERSION"
 * @param  resp     应答存放缓冲区 (可为 NULL，表示不关心应答内容)
 * @param  respSize 应答缓冲区大小
 * @retval 1=收到应答, 0=超时无应答
 */
uint8_t Bluetooth_AT_Send(const char *cmd, char *resp, uint16_t respSize);

/**
 * @brief  测试通信 (发送 "AT"，期望返回 "+OK")
 * @retval 1=成功, 0=失败
 */
uint8_t Bluetooth_AT_Test(void);

/**
 * @brief  查询固件版本
 * @param  ver     版本字符串存放缓冲区
 * @param  verSize 缓冲区大小
 * @retval 1=成功, 0=失败
 */
uint8_t Bluetooth_AT_GetVersion(char *ver, uint16_t verSize);

/**
 * @brief  查询 MAC 地址
 * @param  addr     MAC 地址字符串存放缓冲区
 * @param  addrSize 缓冲区大小
 * @retval 1=成功, 0=失败
 */
uint8_t Bluetooth_AT_GetAddr(char *addr, uint16_t addrSize);

/**
 * @brief  设置蓝牙广播名称
 * @param  name  名称字符串 (最长 18 字节)
 * @retval 1=成功, 0=失败
 */
uint8_t Bluetooth_AT_SetName(const char *name);

/**
 * @brief  设置配对密码
 * @param  pin  4~6 位数字密码字符串
 * @retval 1=成功, 0=失败
 */
uint8_t Bluetooth_AT_SetPIN(const char *pin);

/**
 * @brief  设置波特率
 * @param  baud  BT_Baud_t 枚举值
 * @retval 1=成功, 0=失败
 * @note   设置后模块以新波特率通信，需同步修改 MCU 波特率
 */
uint8_t Bluetooth_AT_SetBaud(BT_Baud_t baud);

/**
 * @brief  软件复位模块
 * @retval 1=成功, 0=失败
 */
uint8_t Bluetooth_AT_Reset(void);

/**
 * @brief  恢复出厂设置
 * @retval 1=成功, 0=失败
 */
uint8_t Bluetooth_AT_Default(void);

/**
 * @brief  断开当前蓝牙连接
 * @retval 1=成功, 0=失败
 */
uint8_t Bluetooth_AT_Disconnect(void);

#endif /* __BLUETOOTH_H */
