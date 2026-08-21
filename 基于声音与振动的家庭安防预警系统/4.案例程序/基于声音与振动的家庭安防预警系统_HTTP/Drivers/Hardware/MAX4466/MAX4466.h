#ifndef __MAX4466_H
#define __MAX4466_H

#include "stm32f1xx_hal.h"

#include <stdint.h>

#define MAX4466_GPIO_PORT GPIOA
#define MAX4466_AO_GPIO_PIN GPIO_PIN_0
#define MAX4466_ADC_CHANNEL ADC_CHANNEL_0
#define MAX4466_SAMPLE_RATE_HZ 4000u
#define MAX4466_FRAME_SAMPLES 128u

typedef struct
{
    uint16_t raw_mean;
    uint16_t raw_min;
    uint16_t raw_max;
    uint32_t voltage_mean_mv;
    uint32_t rms;
    uint32_t peak_to_peak;
    uint32_t band_energy_low;
    uint32_t band_energy_mid;
    uint32_t band_energy_high;
    uint32_t total_energy;
    uint16_t band_ratio_pct;
    uint16_t dominant_freq_hz;
} MAX4466_Feature_t;

void MAX4466_Init(void);
uint32_t MAX4466_RawToMilliVolt(uint16_t raw);
void MAX4466_AnalyzeFrame(const uint16_t *samples, uint16_t count, MAX4466_Feature_t *feature);

#endif /* __MAX4466_H */
