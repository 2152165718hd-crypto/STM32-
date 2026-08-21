#ifndef __PH_H
#define __PH_H

#include "stm32f1xx_hal.h"
#include ".\Hardware\BoardADC\BoardADC.h"

#define PH_OUT_ADC_CHANNEL BOARD_ADC_PH
#define PH_TO_ADC_CHANNEL  BOARD_ADC_PH_TO
#define PH_DO_PORT         GPIOA
#define PH_DO_PIN          GPIO_PIN_3

typedef struct
{
    float voltage_v;
    float to_voltage_v;
    float ph;
    uint8_t digital_alarm;
} PH_Data_t;

void PH_Init(void);
float PH_ReadVoltage(void);
float PH_ReadToVoltage(void);
uint8_t PH_ReadDigitalAlarm(void);
PH_Data_t PH_Read(float k, float b);

#endif
