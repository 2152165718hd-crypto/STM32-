#ifndef __PZT_SENSOR_H
#define __PZT_SENSOR_H

#include "stm32f1xx_hal.h"

#include <stdint.h>

#define PZT_GPIO_PORT GPIOA
#define PZT_GPIO_PIN GPIO_PIN_1
#define PZT_ADC_CHANNEL ADC_CHANNEL_1
#define PZT_SAMPLE_RATE_HZ 8000u
#define PZT_FRAME_SAMPLES 256u
#define PZT_DIVIDER_NUMERATOR 37u
#define PZT_DIVIDER_DENOMINATOR 27u

typedef struct
{
    uint16_t raw_mean;
    uint32_t pa1_mean_mv;
    uint32_t sensor_mean_mv;
    uint32_t peak_mv;
    uint32_t energy;
    uint16_t zero_cross_permille;
    uint16_t dominant_freq_hz;
} PZT_Sensor_Feature_t;

void PZT_Sensor_Init(void);
uint32_t PZT_Sensor_RawToPA1MilliVolt(uint16_t raw);
uint32_t PZT_Sensor_RawToSensorMilliVolt(uint16_t raw);
void PZT_Sensor_AnalyzeFrame(const uint16_t *samples, uint16_t count, PZT_Sensor_Feature_t *feature);

#endif /* __PZT_SENSOR_H */
