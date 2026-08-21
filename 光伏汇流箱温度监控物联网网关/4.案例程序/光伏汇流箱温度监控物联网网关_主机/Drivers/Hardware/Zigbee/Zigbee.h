#ifndef __ZIGBEE_H__
#define __ZIGBEE_H__

#include "stm32f1xx_hal.h"
#include ".\APPLICATION\EnvProto\EnvProto.h"

#define ZIGBEE_UART                   USART2
#define ZIGBEE_UART_IRQn              USART2_IRQn
#define ZIGBEE_UART_BAUDRATE          9600U
#define ZIGBEE_UART_CLK_ENABLE()      __HAL_RCC_USART2_CLK_ENABLE()
#define ZIGBEE_UART_TX_PIN            GPIO_PIN_2
#define ZIGBEE_UART_TX_GPIO_PORT      GPIOA
#define ZIGBEE_UART_TX_CLK_ENABLE()   __HAL_RCC_GPIOA_CLK_ENABLE()
#define ZIGBEE_UART_RX_PIN            GPIO_PIN_3
#define ZIGBEE_UART_RX_GPIO_PORT      GPIOA
#define ZIGBEE_UART_RX_CLK_ENABLE()   __HAL_RCC_GPIOA_CLK_ENABLE()

#define ZIGBEE_RX_BUFFER_SIZE         128U
#define ZIGBEE_FRAME_QUEUE_SIZE       4U

void Zigbee_Init(void);
uint8_t Zigbee_SendFrame(const uint8_t *data, uint16_t len);
void Zigbee_Task(void);
uint8_t Zigbee_GetFrame(EnvProto_Frame_t *frame);

#endif /* __ZIGBEE_H__ */
