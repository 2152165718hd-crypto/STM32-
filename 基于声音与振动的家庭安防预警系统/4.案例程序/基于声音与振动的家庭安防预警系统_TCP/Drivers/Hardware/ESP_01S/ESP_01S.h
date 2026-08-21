#ifndef __ESP_01S_H
#define __ESP_01S_H

#include "stm32f1xx_hal.h"

#include <stdbool.h>
#include <stdint.h>

#define ESP_PORT GPIOA
#define ESP_RX_PIN GPIO_PIN_10
#define ESP_TX_PIN GPIO_PIN_9
#define ESP_UART USART1
#define ESP_GPIO_CLK_ENABLE() __HAL_RCC_GPIOA_CLK_ENABLE()
#define ESP_UART_CLK_ENABLE() __HAL_RCC_USART1_CLK_ENABLE()
#define ESP_UART_IRQn USART1_IRQn

#define ESP_WIFI_SSID "ESP8266_AP"
#define ESP_WIFI_PASS "12345678"
#define ESP_AP_IP "192.168.4.1"
#define ESP_SERVER_PORT "5000"
#define ESP_UART_BAUD 115200u

#define ESP_RX_BUF_SIZE 4096u
#define ESP_TX_BUF_SIZE 1400u
#define ESP_DATA_BUF_SIZE 288u
#define ESP_AT_TIMEOUT 3000u

typedef struct
{
    uint8_t data[ESP_DATA_BUF_SIZE];
    uint16_t len;
    uint8_t link_id;
} ESP_DataPacket_t;

typedef void (*ESP_RxCallback_t)(ESP_DataPacket_t *packet);

typedef enum
{
    ESP_OK = 0,
    ESP_ERR_TIMEOUT,
    ESP_ERR_AT_FAIL,
    ESP_ERR_SEND_FAIL,
    ESP_ERR_BUSY
} ESP_Status_t;

typedef enum
{
    ESP_LINK_EVENT_CONNECT = 0,
    ESP_LINK_EVENT_CLOSED
} ESP_LinkEvent_t;

typedef void (*ESP_LinkEventCallback_t)(uint8_t link_id, ESP_LinkEvent_t event);

ESP_Status_t ESP_Init(void);
void ESP_RegisterCallback(ESP_RxCallback_t cb);
void ESP_RegisterLinkEventCallback(ESP_LinkEventCallback_t cb);
ESP_Status_t ESP_SendData(uint8_t link_id, const uint8_t *data, uint16_t len);
ESP_Status_t ESP_SendDataParts(uint8_t link_id, const uint8_t *part1, uint16_t len1, const uint8_t *part2, uint16_t len2);
ESP_Status_t ESP_SendString(uint8_t link_id, const char *str);
ESP_Status_t ESP_CloseLink(uint8_t link_id);
ESP_Status_t ESP_RestartServer(void);
void ESP_Process(void);
void ESP_RefreshLinkMask(uint8_t link_mask);
ESP_Status_t ESP_SendAT(const char *cmd, const char *ack, uint32_t timeout);
void ESP_IRQHandler(void);
void ESP_RxCallback(void);
void ESP_ErrorCallback(UART_HandleTypeDef *huart);
uint8_t ESP_GetLastErrorStep(void);
uint32_t ESP_GetActiveBaud(void);
uint8_t ESP_GetActiveLinkMask(void);
uint32_t ESP_GetUartErrorCount(void);
uint32_t ESP_GetRxOverflowCount(void);

#endif
