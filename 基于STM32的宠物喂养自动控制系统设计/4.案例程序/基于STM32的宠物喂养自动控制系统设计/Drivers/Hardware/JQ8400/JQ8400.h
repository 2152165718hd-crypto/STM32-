#ifndef __JQ8400_H
#define __JQ8400_H

#include "stm32f1xx_hal.h"
#include <stdint.h>

/**
 * @file JQ8400.h
 * @brief JQ8400-FL 两线串口驱动接口声明。
 */

/* ==================== 硬件配置 ==================== */
#define JQ8400_USART USART1
#define JQ8400_BAUDRATE 9600U

#define JQ8400_TX_PORT GPIOA
#define JQ8400_TX_PIN GPIO_PIN_9

#define JQ8400_RX_PORT GPIOA
#define JQ8400_RX_PIN GPIO_PIN_10

#define JQ8400_USE_BUSY_PIN 1U
#define JQ8400_BUSY_PORT GPIOA
#define JQ8400_BUSY_PIN GPIO_PIN_11

/* 大多数模块 BUSY 低电平表示播放中，如你的模块相反请改为 GPIO_PIN_SET。 */
#define JQ8400_BUSY_ACTIVE_LEVEL GPIO_PIN_RESET

/* 若使用串口重映射，可替换为对应宏（例如 __HAL_AFIO_REMAP_USART1_ENABLE()）。 */
#define JQ8400_USART_REMAP_ENABLE() ((void)0U)

#define JQ8400_UART_TIMEOUT_MS 1000U
#define JQ8400_CMD_INTERVAL_MS 20U

typedef enum
{
    DEVICE_USB = 0x00,
    DEVICE_SD = 0x01,
    DEVICE_FLASH = 0x02
} JQ8400_Device_t;

typedef enum
{
    PLAYMODE_ALL_LOOP = 0x00,      // 全盘循环
    PLAYMODE_SINGLE_LOOP = 0x01,   // 单曲循环
    PLAYMODE_SINGLE_STOP = 0x02,   // 单曲停止
    PLAYMODE_ALL_RANDOM = 0x03,    // 全盘随机
    PLAYMODE_FOLDER_LOOP = 0x04,   // 目录循环
    PLAYMODE_FOLDER_RANDOM = 0x05, // 目录随机
    PLAYMODE_FOLDER_SEQ = 0x06,    // 目录顺序播放
    PLAYMODE_ALL_SEQ = 0x07        // 全盘顺序播放
} JQ8400_PlayMode_t;

typedef enum
{
    EQ_NORMAL = 0x00,
    EQ_POP = 0x01,
    EQ_ROCK = 0x02,
    EQ_JAZZ = 0x03,
    EQ_CLASSIC = 0x04
} JQ8400_EQ_t;

/* 初始化函数，必须先调用（在 HAL_Init() 和系统时钟配置之后） */
void JQ8400_Init(void);

/**
 * @brief 读取 BUSY 状态。
 * @return 1: 模块忙（正在播放） 0: 空闲/未启用 BUSY 引脚。
 */
uint8_t JQ8400_IsBusy(void);

/* 基本播放控制 */
/**
 * @brief 开始播放当前曲目（从头开始）
 */
void JQ8400_Play(void);

/**
 * @brief 暂停播放
 */
void JQ8400_Pause(void);

/**
 * @brief 停止播放
 */
void JQ8400_Stop(void);

/**
 * @brief 上一曲
 */
void JQ8400_Previous(void);

/**
 * @brief 下一曲
 */
void JQ8400_Next(void);

/**
 * @brief 音量加1
 */
void JQ8400_VolumeUp(void);

/**
 * @brief 音量减1
 */
void JQ8400_VolumeDown(void);

/**
 * @brief 设置音量
 * @param volume 0-30（30最大）
 */
void JQ8400_SetVolume(uint8_t volume);

/**
 * @brief 设置EQ音效
 * @param eq 音效类型
 */
void JQ8400_SetEQ(JQ8400_EQ_t eq);

/**
 * @brief 设置播放模式
 * @param mode 播放模式
 */
void JQ8400_SetPlayMode(JQ8400_PlayMode_t mode);

/**
 * @brief 切换存储设备（切换后自动停止，曲目回到第1首）
 * @param dev 设备类型
 */
void JQ8400_SelectDevice(JQ8400_Device_t dev);

/**
 * @brief 指定曲目播放（曲目号与拷贝顺序相关，1~65535）
 * @param track 曲目号（从1开始）
 */
void JQ8400_PlayTrack(uint16_t track);

/**
 * @brief 指定曲目插播（插播结束后自动返回原曲目断点继续播放）
 * @param dev  插播曲目所在设备
 * @param track 插播曲目号（1~65535）
 */
void JQ8400_InsertTrack(JQ8400_Device_t dev, uint16_t track);

/**
 * @brief 结束当前插播或组合播放（返回之前状态）
 */
void JQ8400_EndInsert(void);

#endif /* __JQ8400_H */
