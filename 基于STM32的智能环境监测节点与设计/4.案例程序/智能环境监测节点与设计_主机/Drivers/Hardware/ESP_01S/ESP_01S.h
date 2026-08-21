#ifndef __ESP_01S_H
#define __ESP_01S_H

#include "stm32f1xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

/* WiFi configuration. Keep these values aligned with your root
 * onenet_device_info.json before flashing the host firmware. */
#define ESP_WIFI_SSID "don1ng"
#define ESP_WIFI_PASS "88888888"

#define ESP_TCP_HOST "mqtts.heclouds.com"
#define ESP_TCP_PORT 1883
#define ESP_UART_BAUD 115200

#define ESP_UART USART1
#define ESP_UART_IRQn USART1_IRQn
#define ESP_UART_CLK_ENABLE() __HAL_RCC_USART1_CLK_ENABLE()
#define ESP_UART_GPIO_CLK_ENABLE() __HAL_RCC_GPIOA_CLK_ENABLE()
#define ESP_UART_TX_PIN GPIO_PIN_9
#define ESP_UART_TX_GPIO_PORT GPIOA
#define ESP_UART_RX_PIN GPIO_PIN_10
#define ESP_UART_RX_GPIO_PORT GPIOA

#define ESP_RX_BUF_SIZE 512
#define ESP_TX_BUF_SIZE 1024
#define ESP_DATA_BUF_SIZE 512
#define ESP_AT_TIMEOUT 3000U
#define ESP_WIFI_JOIN_TIMEOUT 20000U
#define ESP_TCP_TIMEOUT 10000U
#define ESP_RECONNECT_INTERVAL_MS 5000U
#define ESP_SEND_PROMPT_TIMEOUT 500U
#define ESP_SEND_RESULT_TIMEOUT 1000U

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

ESP_Status_t ESP_Init(void);
void ESP_RegisterCallback(ESP_RxCallback_t cb);
ESP_Status_t ESP_SendData(uint8_t link_id, const uint8_t *data, uint16_t len);
ESP_Status_t ESP_SendDataAsync(uint8_t link_id, const uint8_t *data, uint16_t len);
ESP_Status_t ESP_SendString(uint8_t link_id, const char *str);
void ESP_Process(void);
ESP_Status_t ESP_SendAT(const char *cmd, const char *ack, uint32_t timeout);
uint8_t ESP_IsWifiConnected(void);
uint8_t ESP_IsConnected(void);

#endif
