#ifndef __LOW_POWER_H
#define __LOW_POWER_H

#include "stm32f1xx_hal.h"

uint8_t LowPower_Init(void);
uint8_t LowPower_EnterStop(uint32_t seconds);
uint8_t LowPower_WasRtcAlarm(void);
void LowPower_ClearWakeFlag(void);
void LowPower_RTCAlarmIRQHandler(void);
RTC_HandleTypeDef *LowPower_GetRtcHandle(void);

#endif
