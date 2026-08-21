#ifndef __SR505_H
#define __SR505_H

#include "stm32f1xx_hal.h"

#define SR505_GPIO_PORT GPIOA
#define SR505_GPIO_PIN GPIO_PIN_0

/* Filter config */
#define SR505_SAMPLE_INTERVAL_MS      20U
#define SR505_WARMUP_MS               30000U
#define SR505_RISE_CONFIRM_MS         500U
#define SR505_FALL_CONFIRM_MS         800U
#define SR505_RETRIGGER_BLOCK_MS      3000U

void SR505_Init(void);
uint8_t SR505_ReadFiltered(void);
uint8_t SR505_IsWarmupMasking(void);
#endif /* __SR505_H */
