#include ".\Hardware\PZT_Sensor\PZT_Sensor.h"

#include <math.h>
#include <string.h>

#define PZT_PI 3.14159265358979323846
#define PZT_Q15_SCALE 32767

/* Preallocated buffers for deterministic per-frame runtime on MCU. */
static int16_t s_window_q15[PZT_FRAME_SAMPLES];
static int16_t s_twiddle_cos_q15[PZT_FRAME_SAMPLES / 2u];
static int16_t s_twiddle_sin_q15[PZT_FRAME_SAMPLES / 2u];
static int32_t s_fft_real[PZT_FRAME_SAMPLES];
static int32_t s_fft_imag[PZT_FRAME_SAMPLES];
static int32_t s_centered_mv[PZT_FRAME_SAMPLES];
static uint8_t s_lookup_ready = 0u;

static int32_t PZT_MulQ15(int32_t value, int16_t q15)
{
    return (int32_t)(((int64_t)value * (int64_t)q15) >> 15);
}

static uint16_t PZT_BitReverse(uint16_t index)
{
    uint16_t reversed = 0u;
    uint8_t bit = 0u;

    for (bit = 0u; bit < 7u; bit++)
    {
        reversed = (uint16_t)((reversed << 1) | (index & 0x1u));
        index >>= 1;
    }

    return reversed;
}

static void PZT_InitLookup(void)
{
    uint16_t i = 0u;

    if (s_lookup_ready != 0u)
    {
        return;
    }

    /* Hann window for better FFT band localization. */
    for (i = 0u; i < PZT_FRAME_SAMPLES; i++)
    {
        double ratio = (double)i / (double)(PZT_FRAME_SAMPLES - 1u);
        double window = 0.5 - 0.5 * cos(2.0 * PZT_PI * ratio);
        s_window_q15[i] = (int16_t)(window * PZT_Q15_SCALE);
    }

    /* Twiddle factors for in-place radix-2 FFT. */
    for (i = 0u; i < (PZT_FRAME_SAMPLES / 2u); i++)
    {
        double angle = (2.0 * PZT_PI * (double)i) / (double)PZT_FRAME_SAMPLES;
        s_twiddle_cos_q15[i] = (int16_t)(cos(angle) * PZT_Q15_SCALE);
        s_twiddle_sin_q15[i] = (int16_t)(-sin(angle) * PZT_Q15_SCALE);
    }

    s_lookup_ready = 1u;
}

static void PZT_FFT128(int32_t *real, int32_t *imag)
{
    uint16_t i = 0u;
    uint16_t len = 0u;

    /* Bit-reversal permutation for iterative FFT implementation. */
    for (i = 0u; i < PZT_FRAME_SAMPLES; i++)
    {
        uint16_t reversed = PZT_BitReverse(i);
        if (reversed > i)
        {
            int32_t temp_real = real[i];
            int32_t temp_imag = imag[i];
            real[i] = real[reversed];
            imag[i] = imag[reversed];
            real[reversed] = temp_real;
            imag[reversed] = temp_imag;
        }
    }

    /* Radix-2 butterflies with per-stage scaling for overflow control. */
    for (len = 2u; len <= PZT_FRAME_SAMPLES; len <<= 1)
    {
        uint16_t half_len = (uint16_t)(len >> 1);
        uint16_t step = (uint16_t)(PZT_FRAME_SAMPLES / len);
        uint16_t start = 0u;

        for (start = 0u; start < PZT_FRAME_SAMPLES; start = (uint16_t)(start + len))
        {
            uint16_t j = 0u;
            uint16_t twiddle_index = 0u;

            for (j = 0u; j < half_len; j++, twiddle_index = (uint16_t)(twiddle_index + step))
            {
                uint16_t even_index = (uint16_t)(start + j);
                uint16_t odd_index = (uint16_t)(even_index + half_len);
                int32_t odd_real = real[odd_index];
                int32_t odd_imag = imag[odd_index];
                int32_t tw_real = PZT_MulQ15(odd_real, s_twiddle_cos_q15[twiddle_index]) -
                                  PZT_MulQ15(odd_imag, s_twiddle_sin_q15[twiddle_index]);
                int32_t tw_imag = PZT_MulQ15(odd_real, s_twiddle_sin_q15[twiddle_index]) +
                                  PZT_MulQ15(odd_imag, s_twiddle_cos_q15[twiddle_index]);
                int32_t even_real = real[even_index];
                int32_t even_imag = imag[even_index];

                real[even_index] = (even_real + tw_real) >> 1;
                imag[even_index] = (even_imag + tw_imag) >> 1;
                real[odd_index] = (even_real - tw_real) >> 1;
                imag[odd_index] = (even_imag - tw_imag) >> 1;
            }
        }
    }
}

void PZT_Sensor_Init(void)
{
    PZT_InitLookup();
}

uint32_t PZT_Sensor_RawToPA1MilliVolt(uint16_t raw)
{
    return ((uint32_t)raw * 3300u) / 4095u;
}

uint32_t PZT_Sensor_RawToSensorMilliVolt(uint16_t raw)
{
    return (PZT_Sensor_RawToPA1MilliVolt(raw) * PZT_DIVIDER_NUMERATOR) / PZT_DIVIDER_DENOMINATOR;
}

void PZT_Sensor_AnalyzeFrame(const uint16_t *samples, uint16_t count, PZT_Sensor_Feature_t *feature)
{
    uint64_t sum_raw = 0u;
    uint64_t sum_sq = 0u;
    uint64_t peak_energy = 0u;
    uint16_t dominant_bin = 0u;
    uint16_t used_count = count;
    uint16_t i = 0u;
    int32_t mean_raw = 0;
    int32_t mean_sensor_mv = 0;
    int32_t prev_sign = 0;
    uint16_t zero_cross_count = 0u;

    if ((samples == NULL) || (feature == NULL))
    {
        return;
    }

    PZT_InitLookup();
    memset(feature, 0, sizeof(PZT_Sensor_Feature_t));

    if (used_count > PZT_FRAME_SAMPLES)
    {
        used_count = PZT_FRAME_SAMPLES;
    }
    if (used_count == 0u)
    {
        return;
    }

    /* Estimate DC operating point of the piezo channel. */
    for (i = 0u; i < used_count; i++)
    {
        sum_raw += samples[i];
    }

    mean_raw = (int32_t)(sum_raw / used_count);
    feature->raw_mean = (uint16_t)mean_raw;
    feature->pa1_mean_mv = PZT_Sensor_RawToPA1MilliVolt((uint16_t)mean_raw);
    feature->sensor_mean_mv = PZT_Sensor_RawToSensorMilliVolt((uint16_t)mean_raw);
    mean_sensor_mv = (int32_t)feature->sensor_mean_mv;

    /* Reset frame work buffers. */
    for (i = 0u; i < PZT_FRAME_SAMPLES; i++)
    {
        s_centered_mv[i] = 0;
        s_fft_real[i] = 0;
        s_fft_imag[i] = 0;
    }

    /*
     * Per-sample feature extraction:
     * 1) remove DC and convert to sensor-side mV
     * 2) peak_mv: maximum absolute deviation
     * 3) energy: mean square of centered waveform
     * 4) zero_cross_permille: sign-change density, reflecting vibration activity
     * 5) windowed data for FFT dominant-frequency estimation
     */
    for (i = 0u; i < used_count; i++)
    {
        int32_t sensor_mv = (int32_t)PZT_Sensor_RawToSensorMilliVolt(samples[i]);
        int32_t sample_sign = 0;
        int32_t abs_mv = 0;

        s_centered_mv[i] = sensor_mv - mean_sensor_mv;
        abs_mv = (s_centered_mv[i] >= 0) ? s_centered_mv[i] : -s_centered_mv[i];
        if ((uint32_t)abs_mv > feature->peak_mv)
        {
            feature->peak_mv = (uint32_t)abs_mv;
        }

        sum_sq += (uint64_t)((int64_t)s_centered_mv[i] * (int64_t)s_centered_mv[i]);
        s_fft_real[i] = PZT_MulQ15(s_centered_mv[i], s_window_q15[i]);

        if (s_centered_mv[i] > 0)
        {
            sample_sign = 1;
        }
        else if (s_centered_mv[i] < 0)
        {
            sample_sign = -1;
        }

        if ((sample_sign != 0) && (prev_sign != 0) && (sample_sign != prev_sign))
        {
            zero_cross_count++;
        }

        if (sample_sign != 0)
        {
            prev_sign = sample_sign;
        }
    }

    /* Average time-domain energy of centered vibration signal. */
    feature->energy = (uint32_t)(sum_sq / used_count);
    if (used_count > 1u)
    {
        /* Per-mille form to keep integer precision on MCU. */
        feature->zero_cross_permille = (uint16_t)((zero_cross_count * 1000u) / (used_count - 1u));
    }

    PZT_FFT128(s_fft_real, s_fft_imag);

    /*
     * Search dominant frequency in low-mid band only (bin 1..25).
     * With Fs/N = 31.25 Hz/bin, this corresponds to about 31..781 Hz,
     * matching typical intrusion vibration content while ignoring higher noise.
     */
    for (i = 1u; i <= 25u; i++)
    {
        uint64_t magnitude_sq = (uint64_t)((int64_t)s_fft_real[i] * (int64_t)s_fft_real[i]) +
                                (uint64_t)((int64_t)s_fft_imag[i] * (int64_t)s_fft_imag[i]);
        if (magnitude_sq > peak_energy)
        {
            peak_energy = magnitude_sq;
            dominant_bin = i;
        }
    }

    /* Convert dominant bin index to frequency in Hz. */
    feature->dominant_freq_hz = (uint16_t)((dominant_bin * PZT_SAMPLE_RATE_HZ) / PZT_FRAME_SAMPLES);
}
