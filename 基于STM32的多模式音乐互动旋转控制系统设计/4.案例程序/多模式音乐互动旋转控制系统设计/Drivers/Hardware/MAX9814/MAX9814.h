#ifndef __MAX9814_H__
#define __MAX9814_H__

#include <stdint.h>

#include "stm32f1xx_hal.h"

#define MAX9814_GAIN_PORT GPIOA
#define MAX9814_GAIN_PIN GPIO_PIN_0
#define MAX9814_OUT_PORT GPIOA
#define MAX9814_OUT_PIN GPIO_PIN_1
#define MAX9814_AR_PORT GPIOA
#define MAX9814_AR_PIN GPIO_PIN_2

#define MAX9814_ADC_INSTANCE ADC1
#define MAX9814_ADC_CHANNEL ADC_CHANNEL_1
#define MAX9814_ADC_CLOCK_DIV RCC_ADCPCLK2_DIV6

void MAX9814_Init(void);
void MAX9814_Task(void);
uint16_t MAX9814_ReadRaw(void);
uint16_t MAX9814_ReadMusicLevelPermille(void);
uint16_t MAX9814_ReadActivityLevelPermille(void);
void MAX9814_DMA_IRQHandler(void);

#endif /* __MAX9814_H__ */
