#ifndef __LU6288_H
#define __LU6288_H

/**
 * @file LU6288.h
 * @brief LU6288 语音播报模块驱动接口声明。
 */

#include "stm32f1xx_hal.h"

/* UART pins */
#define LU6288_GPIO_PORT GPIOA
#define LU6288_TX_PIN GPIO_PIN_9
#define LU6288_RX_PIN GPIO_PIN_10

/* BUSY pin: active high while the module is speaking */
#define LU6288_BUSY_GPIO_PORT GPIOB
#define LU6288_BUSY_PIN GPIO_PIN_3
#define LU6288_BUSY_ACTIVE_LEVEL GPIO_PIN_SET

/* Queue settings */
#define LU6288_QUEUE_SIZE 8U
#define LU6288_MAX_TEXT_LEN 250U

/**
 * @brief 初始化 LU6288 模块串口与忙状态输入脚。
 */
void LU6288_Init(void);

/**
 * @brief 处理语音发送队列与忙状态机。
 */
void LU6288_Process(void);

/**
 * @brief 串口接收完成回调。
 */
void LU6288_RxCpltCallback(void);

/**
 * @brief 播报一段文本。
 * @param text 待播报文本。
 */
void LU6288_Speak(const char *text);

/**
 * @brief 立即停止当前播报并清空发送队列。
 */
void LU6288_Stop(void);

/**
 * @brief 播放指定编号的背景音乐。
 * @param id 音频编号。
 */
void LU6288_PlayBGM(uint8_t id);

/**
 * @brief 播放指定编号的提示音。
 * @param id 音频编号。
 */
void LU6288_PlayTone(uint8_t id);

/**
 * @brief 查询语音模块是否处于忙状态。
 * @return 忙返回 `1`，空闲返回 `0`。
 */
uint8_t LU6288_IsBusy(void);

#endif /* __LU6288_H */
