// JQ8400.h
#ifndef __JQ8400_H
#define __JQ8400_H

#include "stm32f1xx_hal.h"
#include <stdint.h>

/* JQ8400-FL 两线串口驱动 (标准UART 9600 8N1)
 * 适用于 STM32F103C8T6，使用 USART1 (PA9-TX, PA10-RX)
 * 模块 RX 接 MCU TX (PA9)
 * 模块 TX 接 MCU RX (PA10)
 * 电平为 3.3V TTL
 */

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

#endif
