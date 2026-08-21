#include ".\Hardware\SignalFFT\SignalFFT.h"

#include <math.h>

#define SIGNAL_FFT_PI 3.14159265358979323846
#define SIGNAL_FFT_Q15_SCALE 32767

static int16_t s_window_q15[SIGNAL_FFT_FRAME_SAMPLES];
static int16_t s_twiddle_cos_q15[SIGNAL_FFT_FRAME_SAMPLES / 2u];
static int16_t s_twiddle_sin_q15[SIGNAL_FFT_FRAME_SAMPLES / 2u];
static int16_t s_fft_real[SIGNAL_FFT_FRAME_SAMPLES];
static int16_t s_fft_imag[SIGNAL_FFT_FRAME_SAMPLES];
static uint8_t s_lookup_ready = 0u;

static uint16_t SignalFFT_BitReverse(uint16_t index)
{
    uint16_t reversed = 0u;
    uint8_t bit = 0u;
    uint16_t temp = SIGNAL_FFT_FRAME_SAMPLES;
    uint8_t bit_count = 0u;

    while (temp > 1u)
    {
        bit_count++;
        temp >>= 1;
    }

    for (bit = 0u; bit < bit_count; bit++)
    {
        reversed = (uint16_t)((reversed << 1) | (index & 0x1u));
        index >>= 1;
    }

    return reversed;
}

void SignalFFT_Init256(void)
{
    uint16_t i = 0u;

    if (s_lookup_ready != 0u)
    {
        return;
    }

    for (i = 0u; i < SIGNAL_FFT_FRAME_SAMPLES; i++)
    {
        double ratio = (double)i / (double)(SIGNAL_FFT_FRAME_SAMPLES - 1u);
        double window = 0.5 - 0.5 * cos(2.0 * SIGNAL_FFT_PI * ratio);
        s_window_q15[i] = (int16_t)(window * SIGNAL_FFT_Q15_SCALE);
    }

    for (i = 0u; i < (SIGNAL_FFT_FRAME_SAMPLES / 2u); i++)
    {
        double angle = (2.0 * SIGNAL_FFT_PI * (double)i) / (double)SIGNAL_FFT_FRAME_SAMPLES;
        s_twiddle_cos_q15[i] = (int16_t)(cos(angle) * SIGNAL_FFT_Q15_SCALE);
        s_twiddle_sin_q15[i] = (int16_t)(-sin(angle) * SIGNAL_FFT_Q15_SCALE);
    }

    s_lookup_ready = 1u;
}

int16_t SignalFFT_MulQ15(int32_t value, int16_t q15)
{
    return (int16_t)(((int64_t)value * (int64_t)q15) >> 15);
}

const int16_t *SignalFFT_Window256(void)
{
    SignalFFT_Init256();
    return s_window_q15;
}

int16_t *SignalFFT_Real256(void)
{
    return s_fft_real;
}

int16_t *SignalFFT_Imag256(void)
{
    return s_fft_imag;
}

void SignalFFT_Run256(int16_t *real, int16_t *imag)
{
    uint16_t i = 0u;
    uint16_t len = 0u;

    SignalFFT_Init256();

    for (i = 0u; i < SIGNAL_FFT_FRAME_SAMPLES; i++)
    {
        uint16_t reversed = SignalFFT_BitReverse(i);
        if (reversed > i)
        {
            int16_t temp_real = real[i];
            int16_t temp_imag = imag[i];
            real[i] = real[reversed];
            imag[i] = imag[reversed];
            real[reversed] = temp_real;
            imag[reversed] = temp_imag;
        }
    }

    for (len = 2u; len <= SIGNAL_FFT_FRAME_SAMPLES; len <<= 1)
    {
        uint16_t half_len = (uint16_t)(len >> 1);
        uint16_t step = (uint16_t)(SIGNAL_FFT_FRAME_SAMPLES / len);
        uint16_t start = 0u;

        for (start = 0u; start < SIGNAL_FFT_FRAME_SAMPLES; start = (uint16_t)(start + len))
        {
            uint16_t j = 0u;
            uint16_t twiddle_index = 0u;

            for (j = 0u; j < half_len; j++, twiddle_index = (uint16_t)(twiddle_index + step))
            {
                uint16_t even_index = (uint16_t)(start + j);
                uint16_t odd_index = (uint16_t)(even_index + half_len);
                int32_t odd_real = real[odd_index];
                int32_t odd_imag = imag[odd_index];
                int32_t tw_real = (int32_t)SignalFFT_MulQ15(odd_real, s_twiddle_cos_q15[twiddle_index]) -
                                  (int32_t)SignalFFT_MulQ15(odd_imag, s_twiddle_sin_q15[twiddle_index]);
                int32_t tw_imag = (int32_t)SignalFFT_MulQ15(odd_real, s_twiddle_sin_q15[twiddle_index]) +
                                  (int32_t)SignalFFT_MulQ15(odd_imag, s_twiddle_cos_q15[twiddle_index]);
                int32_t even_real = real[even_index];
                int32_t even_imag = imag[even_index];

                real[even_index] = (int16_t)((even_real + tw_real) >> 1);
                imag[even_index] = (int16_t)((even_imag + tw_imag) >> 1);
                real[odd_index] = (int16_t)((even_real - tw_real) >> 1);
                imag[odd_index] = (int16_t)((even_imag - tw_imag) >> 1);
            }
        }
    }
}
