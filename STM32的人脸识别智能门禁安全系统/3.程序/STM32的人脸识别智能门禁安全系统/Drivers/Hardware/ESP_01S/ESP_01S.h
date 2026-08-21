#ifndef __ESP_01S_H
#define __ESP_01S_H

#include "stm32f1xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/* ---------------------- 用户配置区 ---------------------- */
#define ESP_WIFI_SSID "ESP8266_AP" /* AP 热点名称 */
#define ESP_WIFI_PASS "12345678"   /* AP 密码(>=8位) */
#define ESP_SERVER_PORT "8080"     /* TCP 服务器端口 */
#define ESP_UART_BAUD 115200       /* 与 ESP-01S 通信波特率 */

#define ESP_RX_BUF_SIZE 512   /* 接收环形缓冲区大小 */
#define ESP_TX_BUF_SIZE 256   /* 单次发送最大长度 */
#define ESP_DATA_BUF_SIZE 256 /* 解析后净数据缓冲区 */
#define ESP_AT_TIMEOUT 3000   /* AT 指令等待超时 ms */

/* ---------------------- 数据结构 ---------------------- */

/* 接收到的净数据包 */
typedef struct
{
    uint8_t data[ESP_DATA_BUF_SIZE];
    uint16_t len;
    uint8_t link_id; /* 连接号 */
} ESP_DataPacket_t;

/* 数据接收回调函数类型 */
typedef void (*ESP_RxCallback_t)(ESP_DataPacket_t *packet);

/* 模块状态 */
typedef enum
{
    ESP_OK = 0,
    ESP_ERR_TIMEOUT,
    ESP_ERR_AT_FAIL,
    ESP_ERR_SEND_FAIL,
    ESP_ERR_BUSY
} ESP_Status_t;

/* ---------------------- API ---------------------- */

/**
 * @brief  初始化 ESP-01S 模块（含 UART1 硬件初始化 + AT 配置）
 * @retval ESP_OK 成功，其他失败
 */
ESP_Status_t ESP_Init(void);

/**
 * @brief  注册数据接收回调
 * @param  cb  回调函数指针
 */
void ESP_RegisterCallback(ESP_RxCallback_t cb);

/**
 * @brief  向指定连接发送数据
 * @param  link_id  连接号（单连接模式填 0）
 * @param  data     数据指针
 * @param  len      数据长度
 * @retval ESP_OK 成功
 */
ESP_Status_t ESP_SendData(uint8_t link_id, const uint8_t *data, uint16_t len);

/**
 * @brief  向指定连接发送字符串
 */
ESP_Status_t ESP_SendString(uint8_t link_id, const char *str);

/**
 * @brief  主循环中调用，处理接收缓冲区数据
 *         必须在 while(1) 中周期性调用
 */
void ESP_Process(void);

/**
 * @brief  发送原始 AT 指令（高级用途）
 * @param  cmd      AT 指令字符串
 * @param  ack      期望应答关键字
 * @param  timeout  超时 ms
 * @retval ESP_OK 收到期望应答
 */
ESP_Status_t ESP_SendAT(const char *cmd, const char *ack, uint32_t timeout);

/**
 * @brief  ESP-01S UART 接收中断回调中断函数，用户需要在 HAL_UART_RxCpltCallback 中调用
 */
void ESP_RxCallback(void);

/**
 * @brief  ESP-01S UART 错误回调，需在 HAL_UART_ErrorCallback 中调用
 * @param  huart UART 句柄
 */
void ESP_ErrorCallback(UART_HandleTypeDef *huart);

#endif /* __ESP01S_H */
