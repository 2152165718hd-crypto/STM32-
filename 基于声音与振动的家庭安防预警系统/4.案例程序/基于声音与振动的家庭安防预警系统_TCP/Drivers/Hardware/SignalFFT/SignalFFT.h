#ifndef __SIGNAL_FFT_H
#define __SIGNAL_FFT_H

#include <stdint.h>

#define SIGNAL_FFT_FRAME_SAMPLES 256u

void SignalFFT_Init256(void);
void SignalFFT_Run256(int16_t *real, int16_t *imag);
int16_t SignalFFT_MulQ15(int32_t value, int16_t q15);
const int16_t *SignalFFT_Window256(void);
int16_t *SignalFFT_Real256(void);
int16_t *SignalFFT_Imag256(void);

#endif /* __SIGNAL_FFT_H */
