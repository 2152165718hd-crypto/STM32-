#ifndef __BOARD_ADC_H
#define __BOARD_ADC_H

#include "stm32f1xx_hal.h"

#define BOARD_ADC_VREF_MV 3300U

typedef enum
{
    BOARD_ADC_TURBIDITY = ADC_CHANNEL_0,
    BOARD_ADC_PH        = ADC_CHANNEL_2,
    BOARD_ADC_PH_TO     = ADC_CHANNEL_4
} BoardADC_Channel_t;

uint8_t BoardADC_Init(void);
uint16_t BoardADC_ReadRaw(BoardADC_Channel_t channel);
uint16_t BoardADC_ReadAverageRaw(BoardADC_Channel_t channel, uint8_t samples);
float BoardADC_ReadVoltage(BoardADC_Channel_t channel);
float BoardADC_RawToVoltage(uint16_t raw);

#endif
