#ifndef _USART_H
#define _USART_H

#include "stdio.h"
#include "./SYSTEM/sys/sys.h"


/*******************************************************************************************************/
/* 串口引脚/设备配置（默认USART1） */

#define USART_TX_GPIO_PORT              GPIOA
#define USART_TX_GPIO_PIN               GPIO_PIN_9
#define USART_TX_GPIO_AF                GPIO_AF7_USART1
#define USART_TX_GPIO_CLK_ENABLE()      do{ __HAL_RCC_GPIOA_CLK_ENABLE(); }while(0)   /* 使能TX引脚时钟 */

#define USART_RX_GPIO_PORT              GPIOA
#define USART_RX_GPIO_PIN               GPIO_PIN_10
#define USART_RX_GPIO_AF                GPIO_AF7_USART1
#define USART_RX_GPIO_CLK_ENABLE()      do{ __HAL_RCC_GPIOA_CLK_ENABLE(); }while(0)   /* 使能RX引脚时钟 */

#define USART_UX                        USART1
#define USART_UX_IRQn                   USART1_IRQn
#define USART_UX_CLK_ENABLE()           do{ __HAL_RCC_USART1_CLK_ENABLE(); }while(0)  /* 使能串口时钟 */

/*******************************************************************************************************/

#define USART_REC_LEN   200                     /* 最大接收字节数 */
#define USART_EN_RX     1                       /* 使能接收(1)/禁用(0) */
#define RXBUFFERSIZE    1                       /* 接收缓冲区大小 */

extern UART_HandleTypeDef g_uart1_handle;       /* UART句柄 */
extern uint8_t  g_usart_rx_buf[USART_REC_LEN];  /* 接收缓冲区 */
extern uint16_t g_usart_rx_sta;                 /* 接收状态标志 */
extern uint8_t g_rx_buffer[RXBUFFERSIZE];       /* HAL库接收缓冲区 */


void usart_init(uint32_t baudrate);             /* 初始化串口 */
void USART_Debug_RxCpltCallback(UART_HandleTypeDef *huart);

#endif


