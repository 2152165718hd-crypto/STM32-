#ifndef __AIRM2M_4G_H
#define __AIRM2M_4G_H

#include "stm32f1xx_hal.h"
#include <stdint.h>

#ifndef AIRM2M_4G_USART
#define AIRM2M_4G_USART USART3
#endif

#ifndef AIRM2M_4G_BAUDRATE
#define AIRM2M_4G_BAUDRATE 115200U
#endif

#ifndef AIRM2M_4G_UART_RX_PORT
#define AIRM2M_4G_UART_RX_PORT GPIOB
#endif

#ifndef AIRM2M_4G_UART_RX_PIN
#define AIRM2M_4G_UART_RX_PIN GPIO_PIN_11
#endif

#ifndef AIRM2M_4G_UART_TX_PORT
#define AIRM2M_4G_UART_TX_PORT GPIOB
#endif

#ifndef AIRM2M_4G_UART_TX_PIN
#define AIRM2M_4G_UART_TX_PIN GPIO_PIN_10
#endif

#ifndef AIRM2M_4G_USART_REMAP_ENABLE
#define AIRM2M_4G_USART_REMAP_ENABLE() ((void)0U)
#endif

#ifndef AIRM2M_4G_IRQ
#define AIRM2M_4G_IRQ USART3_IRQn
#endif

#ifndef AIRM2M_4G_IRQ_HANDLER
#define AIRM2M_4G_IRQ_HANDLER USART3_IRQHandler
#endif

#ifndef AIRM2M_4G_RX_BUFFER_SIZE
#define AIRM2M_4G_RX_BUFFER_SIZE 1024U
#endif

#ifndef AIRM2M_4G_LINE_BUFFER_SIZE
#define AIRM2M_4G_LINE_BUFFER_SIZE 1024U
#endif

#ifndef AIRM2M_4G_TOPIC_BUFFER_SIZE
#define AIRM2M_4G_TOPIC_BUFFER_SIZE 160U
#endif

#ifndef AIRM2M_4G_PAYLOAD_BUFFER_SIZE
#define AIRM2M_4G_PAYLOAD_BUFFER_SIZE 768U
#endif

#ifndef AIRM2M_4G_AT_TIMEOUT_MS
#define AIRM2M_4G_AT_TIMEOUT_MS 2000U
#endif

#ifndef AIRM2M_4G_NET_TIMEOUT_MS
#define AIRM2M_4G_NET_TIMEOUT_MS 30000U
#endif

#ifndef AIRM2M_4G_MQTT_TIMEOUT_MS
#define AIRM2M_4G_MQTT_TIMEOUT_MS 15000U
#endif

#ifndef AIRM2M_4G_CLOSE_TIMEOUT_MS
#define AIRM2M_4G_CLOSE_TIMEOUT_MS 3000U
#endif

#ifndef AIRM2M_4G_PUBLISH_TIMEOUT_MS
#define AIRM2M_4G_PUBLISH_TIMEOUT_MS 8000U
#endif

typedef enum
{
    AIRM2M_4G_OK = 0,
    AIRM2M_4G_TIMEOUT,
    AIRM2M_4G_ERROR,
    AIRM2M_4G_BUSY,
    AIRM2M_4G_PARAM,
    AIRM2M_4G_NOT_CONNECTED
} AirM2M_4G_Result_t;

typedef enum
{
    AIRM2M_4G_STATUS_RESET = 0,
    AIRM2M_4G_STATUS_AT_READY,
    AIRM2M_4G_STATUS_SIM_READY,
    AIRM2M_4G_STATUS_NET_READY,
    AIRM2M_4G_STATUS_TCP_CONNECTED,
    AIRM2M_4G_STATUS_MQTT_CONNECTED,
    AIRM2M_4G_STATUS_SUBSCRIBED,
    AIRM2M_4G_STATUS_CLOSED,
    AIRM2M_4G_STATUS_ERROR
} AirM2M_4G_Status_t;

typedef void (*AirM2M_4G_MqttMessageCallback_t)(const char *topic,
                                                 const char *payload,
                                                 uint16_t payload_len);
typedef void (*AirM2M_4G_WaitCallback_t)(void);

AirM2M_4G_Result_t AirM2M_4G_Init(void);
void AirM2M_4G_Process(void);

AirM2M_4G_Result_t AirM2M_4G_CheckNetwork(uint32_t timeout_ms);
AirM2M_4G_Result_t AirM2M_4G_MqttConnect(const char *host,
                                          uint16_t port,
                                          const char *client_id,
                                          const char *username,
                                          const char *password,
                                          uint16_t keepalive,
                                          uint8_t clean_session);
AirM2M_4G_Result_t AirM2M_4G_MqttSubscribe(const char *topic, uint8_t qos);
AirM2M_4G_Result_t AirM2M_4G_MqttPublish(const char *topic,
                                          const char *payload,
                                          uint8_t qos,
                                          uint8_t retain);
AirM2M_4G_Result_t AirM2M_4G_QueryMqttStatus(void);
AirM2M_4G_Result_t AirM2M_4G_DebugCommand(const char *cmd,
                                           const char *expect1,
                                           const char *expect2,
                                           uint32_t timeout_ms,
                                           char *response,
                                           uint16_t response_size);

void AirM2M_4G_RegisterMqttMessageCallback(AirM2M_4G_MqttMessageCallback_t cb);
void AirM2M_4G_RegisterWaitCallback(AirM2M_4G_WaitCallback_t cb);
AirM2M_4G_Status_t AirM2M_4G_GetStatus(void);
const char *AirM2M_4G_GetStatusText(void);
const char *AirM2M_4G_GetLastLine(void);
uint32_t AirM2M_4G_GetLastLineTick(void);
uint8_t AirM2M_4G_IsMqttConnected(void);
uint8_t AirM2M_4G_IsSubscribed(void);

#endif /* __AIRM2M_4G_H */
