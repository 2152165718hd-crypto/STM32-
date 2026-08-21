#ifndef __DS1302_RTC_H
#define __DS1302_RTC_H

/**
 * @file DS1302_RTC.h
 * @brief DS1302 实时时钟驱动接口声明。
 */

#include "stm32f1xx_hal.h"

/* ---- 引脚定义 ---- */
#define DS1302_PORT GPIOA
#define DS1302_CLK_PIN GPIO_PIN_15
#define DS1302_DAT_PIN GPIO_PIN_12
#define DS1302_RST_PIN GPIO_PIN_11

/* ---- DS1302 寄存器地址 ---- */
#define DS1302_REG_SEC 0x80
#define DS1302_REG_MIN 0x82
#define DS1302_REG_HOUR 0x84
#define DS1302_REG_DATE 0x86
#define DS1302_REG_MON 0x88
#define DS1302_REG_DAY 0x8A
#define DS1302_REG_YEAR 0x8C
#define DS1302_REG_WP 0x8E
#define DS1302_REG_CHARGE 0x90

/* ---- 时间结构体 ---- */
/** @brief DS1302 时间结构。 */
typedef struct
{
    uint8_t year;   /* 00-99 */
    uint8_t month;  /* 01-12 */
    uint8_t date;   /* 01-31 */
    uint8_t day;    /* 01-07 (星期) */
    uint8_t hour;   /* 00-23 */
    uint8_t minute; /* 00-59 */
    uint8_t second; /* 00-59 */
} DS1302_Time_t;

/* ---- 函数声明 ---- */
/**
 * @brief 初始化 DS1302 相关 GPIO 与芯片工作状态。
 */
void DS1302_Init(void);

/**
 * @brief 设置 DS1302 当前时间。
 * @param time 时间结构体指针。
 */
void DS1302_SetTime(DS1302_Time_t *time);

/**
 * @brief 读取 DS1302 当前时间。
 * @param time 时间结构体指针。
 */
void DS1302_GetTime(DS1302_Time_t *time);

#endif /* __DS1302_RTC_H */
