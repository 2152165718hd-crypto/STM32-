#ifndef __TURBIDTY_H
#define __TURBIDTY_H

#include "stm32f1xx_hal.h"

typedef struct
{
    /* Compensated sensor output voltage in volts. */
    float voltage_v;
    float ntu;
} Turbidity_Data_t;

void Turbidity_Init(void);
/* Returns the compensated sensor output voltage, clamped to the practical 0.5V..4.5V sensor window. */
float Turbidity_ReadVoltage(void);
/* Kept for API compatibility; the implementation uses host-provided calibration coefficients. */
float Turbidity_ReadNTU(float a, float b, float c);
Turbidity_Data_t Turbidity_Read(float a, float b, float c);

#endif
