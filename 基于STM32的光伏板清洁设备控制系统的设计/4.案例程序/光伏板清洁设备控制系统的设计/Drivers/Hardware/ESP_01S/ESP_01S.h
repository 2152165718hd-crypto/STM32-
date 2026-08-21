#ifndef __ESP_01S_H
#define __ESP_01S_H

#include "stm32f1xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

#define ESP_TX_PIN GPIO_PIN_9
#define ESP_RX_PIN GPIO_PIN_10
#define ESP_GPIO_PORT GPIOA
#define ESP_UART USART1
#define ESP_UART_IRQn USART1_IRQn

#define ESP_AP_IP_ADDR "192.168.4.1"
#define ESP_WIFI_SSID "ESP8266_AP"
#define ESP_WIFI_PASS "12345678"
#define ESP_SERVER_PORT "5000"
#define ESP_UART_BAUD 115200
#define ESP_MAX_LINKS 5u

#define ESP_RX_BUF_SIZE 768
#define ESP_TX_BUF_SIZE 1024
#define ESP_DATA_BUF_SIZE 512
#define ESP_AT_TIMEOUT 3000

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
    ESP_CONN_INIT_FAIL = 0,
    ESP_CONN_AP_READY,
    ESP_CONN_CLIENT_CONNECTED
} ESP_ConnectionState_t;

ESP_Status_t ESP_Init(void);
void ESP_RegisterCallback(ESP_RxCallback_t cb);
ESP_Status_t ESP_SendData(uint8_t link_id, const uint8_t *data, uint16_t len);
ESP_Status_t ESP_SendString(uint8_t link_id, const char *str);
ESP_Status_t ESP_CloseLink(uint8_t link_id);
void ESP_Process(void);
ESP_Status_t ESP_SendAT(const char *cmd, const char *ack, uint32_t timeout);
void ESP_RxCallback(void);
void ESP_ErrorCallback(UART_HandleTypeDef *huart);
ESP_ConnectionState_t ESP_GetConnectionState(void);
uint8_t ESP_GetActiveLinkId(void);
uint8_t ESP_GetActiveLinkMask(void);
uint8_t ESP_IsLinkActive(uint8_t link_id);
uint8_t ESP_GetClientCount(void);

#endif
