#ifndef __SR602_H
#define __SR602_H

#include "stm32f1xx_hal.h"

#define SR602_GPIO_PORT GPIOA
#define SR602_GPIO_PIN GPIO_PIN_0

/* SR602 出厂默认高电平保持约 2.5s */
#define SR602_OUTPUT_HOLD_MS 2500U

/* SR602 固定封锁时间约 2s（模块特性，不可调） */
#define SR602_BLOCK_TIME_MS 2000U

/*
 * 上电后模块会先输出一段高电平作为启动过程。
 * 如果你修改了模块延时，请同步调整 SR602_OUTPUT_HOLD_MS，
 * 本值会随之自动增大。
 */
#define SR602_STARTUP_IGNORE_MS (SR602_OUTPUT_HOLD_MS + 500U)

/* 软件滤波参数（用于抑制误触发） */
#define SR602_FILTER_SAMPLE_MS 10U
#define SR602_FILTER_STABLE_CNT 3U
#define SR602_VALID_HIGH_MS 80U

void SR602_Init(void);
void SR602_Update(void);

uint8_t SR602_IsReady(void);
uint8_t SR602_GetRawLevel(void);
uint8_t SR602_GetLevel(void);
uint8_t SR602_GetMotionEvent(void);

#endif /* __SR602_H */
