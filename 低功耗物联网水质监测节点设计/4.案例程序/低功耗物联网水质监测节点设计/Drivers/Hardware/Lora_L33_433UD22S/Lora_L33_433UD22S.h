#ifndef __LORA_L33_433UD22S_H
#define __LORA_L33_433UD22S_H

#include "stm32f1xx_hal.h"

#define LORA_UART_INSTANCE USART3
#define LORA_UART_BAUDRATE 9600U

#define LORA_TX_PORT GPIOB
#define LORA_TX_PIN  GPIO_PIN_10
#define LORA_RX_PORT GPIOB
#define LORA_RX_PIN  GPIO_PIN_11

#define LORA_M0_PORT  GPIOB
#define LORA_M0_PIN   GPIO_PIN_0
#define LORA_M1_PORT  GPIOB
#define LORA_M1_PIN   GPIO_PIN_1
#define LORA_AUX_PORT GPIOA
#define LORA_AUX_PIN  GPIO_PIN_7

#define LORA_LINE_MAX 180U

typedef enum
{
    LORA_MODE_NORMAL = 0,
    LORA_MODE_WAKE_UP,
    LORA_MODE_POWER_SAVE,
    LORA_MODE_CONFIG
} Lora_Mode_t;

uint8_t Lora_Init(void);
void Lora_SetMode(Lora_Mode_t mode);
uint8_t Lora_IsAuxReady(void);
uint8_t Lora_HasPendingLine(void);
HAL_StatusTypeDef Lora_SendBytes(const uint8_t *data, uint16_t len);
HAL_StatusTypeDef Lora_SendString(const char *text);
uint8_t Lora_ReadLine(char *line, uint16_t max_len);
void Lora_RestartRxIT(void);
UART_HandleTypeDef *Lora_GetUartHandle(void);

#endif
