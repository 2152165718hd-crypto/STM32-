#include ".\Hardware\MAX4466\MAX4466.h"

#include <math.h>
#include <string.h>

#define MAX4466_PI 3.14159265358979323846
#define MAX4466_Q15_SCALE 32767

/*
 * Lookup tables and frame buffers are static to avoid repeated stack allocation
 * and to keep FFT processing deterministic on MCU.
 */
static int16_t s_window_q15[MAX4466_FRAME_SAMPLES];
static int16_t s_twiddle_cos_q15[MAX4466_FRAME_SAMPLES / 2u];
static int16_t s_twiddle_sin_q15[MAX4466_FRAME_SAMPLES / 2u];
static int32_t s_fft_real[MAX4466_FRAME_SAMPLES];
static int32_t s_fft_imag[MAX4466_FRAME_SAMPLES];
static int32_t s_centered_mv[MAX4466_FRAME_SAMPLES];
static uint8_t s_lookup_ready = 0u;

static uint32_t MAX4466_Isqrt64(uint64_t value)
{
    uint64_t result = 0u;
    uint64_t bit = (uint64_t)1u << 62;

    while (bit > value)
    {
        bit >>= 2;
    }

    while (bit != 0u)
    {
        if (value >= (result + bit))
        {
            value -= (result + bit);
            result = (result >> 1) + bit;
        }
        else
        {
            result >>= 1;
        }
        bit >>= 2;
    }

    return (uint32_t)result;
}

static int32_t MAX4466_MulQ15(int32_t value, int16_t q15)
{
    return (int32_t)(((int64_t)value * (int64_t)q15) >> 15);
}

static uint32_t MAX4466_ClampU64ToU32(uint64_t value)
{
    if (value > 0xFFFFFFFFULL)
    {
        return 0xFFFFFFFFUL;
    }
    return (uint32_t)value;
}

static uint16_t MAX4466_BitReverse(uint16_t index)
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

static void MAX4466_InitLookup(void)
{
    uint16_t i = 0u;

    if (s_lookup_ready != 0u)
    {
        return;
    }

    /*
     * Hann window: suppress spectral leakage before FFT.
     * Q15 is used to reduce runtime floating-point math during frame processing.
     */
    for (i = 0u; i < MAX4466_FRAME_SAMPLES; i++)
    {
        double ratio = (double)i / (double)(MAX4466_FRAME_SAMPLES - 1u);
        double window = 0.5 - 0.5 * cos(2.0 * MAX4466_PI * ratio);
        s_window_q15[i] = (int16_t)(window * MAX4466_Q15_SCALE);
    }

    /*
     * Twiddle factors for radix-2 FFT:
     * W_N^k = cos(2*pi*k/N) - j*sin(2*pi*k/N)
     */
    for (i = 0u; i < (MAX4466_FRAME_SAMPLES / 2u); i++)
    {
        double angle = (2.0 * MAX4466_PI * (double)i) / (double)MAX4466_FRAME_SAMPLES;
        s_twiddle_cos_q15[i] = (int16_t)(cos(angle) * MAX4466_Q15_SCALE);
        s_twiddle_sin_q15[i] = (int16_t)(-sin(angle) * MAX4466_Q15_SCALE);
    }

    s_lookup_ready = 1u;
}

static void MAX4466_FFT128(int32_t *real, int32_t *imag)
{
    uint16_t i = 0u;
    uint16_t len = 0u;

    /* Bit-reversal permutation: reorder input for in-place iterative FFT. */
    for (i = 0u; i < MAX4466_FRAME_SAMPLES; i++)
    {
        uint16_t reversed = MAX4466_BitReverse(i);
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

    /*
     * Iterative radix-2 butterflies.
     * Right shift by 1 at each butterfly limits growth and helps prevent overflow.
     */
    for (len = 2u; len <= MAX4466_FRAME_SAMPLES; len <<= 1)
    {
        uint16_t half_len = (uint16_t)(len >> 1);
        uint16_t step = (uint16_t)(MAX4466_FRAME_SAMPLES / len);
        uint16_t start = 0u;

        for (start = 0u; start < MAX4466_FRAME_SAMPLES; start = (uint16_t)(start + len))
        {
            uint16_t j = 0u;
            uint16_t twiddle_index = 0u;

            for (j = 0u; j < half_len; j++, twiddle_index = (uint16_t)(twiddle_index + step))
            {
                uint16_t even_index = (uint16_t)(start + j);
                uint16_t odd_index = (uint16_t)(even_index + half_len);
                int32_t odd_real = real[odd_index];
                int32_t odd_imag = imag[odd_index];
                int32_t tw_real = MAX4466_MulQ15(odd_real, s_twiddle_cos_q15[twiddle_index]) -
                                  MAX4466_MulQ15(odd_imag, s_twiddle_sin_q15[twiddle_index]);
                int32_t tw_imag = MAX4466_MulQ15(odd_real, s_twiddle_sin_q15[twiddle_index]) +
                                  MAX4466_MulQ15(odd_imag, s_twiddle_cos_q15[twiddle_index]);
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

void MAX4466_Init(void)
{
    MAX4466_InitLookup();
}

uint32_t MAX4466_RawToMilliVolt(uint16_t raw)
{
    return ((uint32_t)raw * 3300u) / 4095u;
}

void MAX4466_AnalyzeFrame(const uint16_t *samples, uint16_t count, MAX4466_Feature_t *feature)
{
    uint64_t sum_raw = 0u;
    uint64_t sum_sq = 0u;
    uint64_t low_energy = 0u;
    uint64_t mid_energy = 0u;
    uint64_t high_energy = 0u;
    uint64_t peak_energy = 0u;
    uint16_t dominant_bin = 0u;
    uint16_t used_count = count;
    uint16_t i = 0u;
    uint16_t raw_min = 0xFFFFu;
    uint16_t raw_max = 0u;
    int32_t mean_raw = 0;
    uint32_t total_energy = 0u;

    if ((samples == NULL) || (feature == NULL))
    {
        return;
    }

    MAX4466_InitLookup();
    memset(feature, 0, sizeof(MAX4466_Feature_t));

    if (used_count > MAX4466_FRAME_SAMPLES)
    {
        used_count = MAX4466_FRAME_SAMPLES;
    }
    if (used_count == 0u)
    {
        return;
    }

    /* First pass: capture DC level and raw dynamic range. */
    for (i = 0u; i < used_count; i++)
    {
        uint16_t sample = samples[i];
        sum_raw += sample;
        if (sample < raw_min)
        {
            raw_min = sample;
        }
        if (sample > raw_max)
        {
            raw_max = sample;
        }
    }

    mean_raw = (int32_t)(sum_raw / used_count);
    feature->raw_mean = (uint16_t)mean_raw;
    feature->raw_min = raw_min;
    feature->raw_max = raw_max;
    feature->voltage_mean_mv = MAX4466_RawToMilliVolt((uint16_t)mean_raw);
    feature->peak_to_peak = ((uint32_t)(raw_max - raw_min) * 3300u) / 4095u;

    /* Clear frame work buffers before filling valid samples. */
    for (i = 0u; i < MAX4466_FRAME_SAMPLES; i++)
    {
        s_centered_mv[i] = 0;
        s_fft_real[i] = 0;
        s_fft_imag[i] = 0;
    }

    /*
     * Second pass:
     * 1) remove DC (mean subtraction)
     * 2) convert centered ADC counts to mV
     * 3) accumulate time-domain RMS energy
     * 4) apply Hann window as FFT input
     */
    for (i = 0u; i < used_count; i++)
    {
        int32_t centered_count = (int32_t)samples[i] - mean_raw;
        s_centered_mv[i] = (centered_count * 3300) / 4095;
        sum_sq += (uint64_t)((int64_t)s_centered_mv[i] * (int64_t)s_centered_mv[i]);
        s_fft_real[i] = MAX4466_MulQ15(s_centered_mv[i], s_window_q15[i]);
    }

    /* RMS of AC component in mV domain. */
    feature->rms = MAX4466_Isqrt64(sum_sq / used_count);

    MAX4466_FFT128(s_fft_real, s_fft_imag);

    /*
     * Frequency resolution = Fs/N = 4000/128 = 31.25 Hz per bin.
     * Band partition:
     *   low : bin  4..11  (~125..343 Hz)
     *   mid : bin 12..27  (~375..843 Hz)
     *   high: bin 28..57  (~875..1781 Hz)
     * Dominant frequency is the strongest bin in 1..63.
     */
    for (i = 1u; i < (MAX4466_FRAME_SAMPLES / 2u); i++)
    {
        uint64_t magnitude_sq = (uint64_t)((int64_t)s_fft_real[i] * (int64_t)s_fft_real[i]) +
                                (uint64_t)((int64_t)s_fft_imag[i] * (int64_t)s_fft_imag[i]);

        if ((i >= 4u) && (i <= 11u))
        {
            low_energy += magnitude_sq;
        }
        else if ((i >= 12u) && (i <= 27u))
        {
            mid_energy += magnitude_sq;
        }
        else if ((i >= 28u) && (i <= 57u))
        {
            high_energy += magnitude_sq;
        }

        if (magnitude_sq > peak_energy)
        {
            peak_energy = magnitude_sq;
            dominant_bin = i;
        }
    }

    feature->band_energy_low = MAX4466_ClampU64ToU32(low_energy);
    feature->band_energy_mid = MAX4466_ClampU64ToU32(mid_energy);
    feature->band_energy_high = MAX4466_ClampU64ToU32(high_energy);
    total_energy = feature->band_energy_low + feature->band_energy_mid + feature->band_energy_high;
    feature->total_energy = total_energy;
    if (total_energy != 0u)
    {
        /* Ratio used by upper-layer decision logic to suppress low-frequency hum/noise. */
        feature->band_ratio_pct = (uint16_t)(((feature->band_energy_mid + feature->band_energy_high) * 100u) / total_energy);
    }

    /* Convert dominant bin to frequency in Hz. */
    feature->dominant_freq_hz = (uint16_t)((dominant_bin * MAX4466_SAMPLE_RATE_HZ) / MAX4466_FRAME_SAMPLES);
}
