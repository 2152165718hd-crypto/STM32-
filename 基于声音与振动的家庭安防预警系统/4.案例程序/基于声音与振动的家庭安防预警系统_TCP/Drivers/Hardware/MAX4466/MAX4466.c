#include ".\Hardware\MAX4466\MAX4466.h"
#include ".\Hardware\SignalFFT\SignalFFT.h"

#include <string.h>

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

static uint32_t MAX4466_ClampU64ToU32(uint64_t value)
{
    if (value > 0xFFFFFFFFULL)
    {
        return 0xFFFFFFFFUL;
    }
    return (uint32_t)value;
}

void MAX4466_Init(void)
{
    SignalFFT_Init256();
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
    uint64_t total_energy_64 = 0u;
    uint32_t total_energy = 0u;
    const int16_t *window_q15;
    int16_t *fft_real;
    int16_t *fft_imag;

    if ((samples == NULL) || (feature == NULL))
    {
        return;
    }

    SignalFFT_Init256();
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

    window_q15 = SignalFFT_Window256();
    fft_real = SignalFFT_Real256();
    fft_imag = SignalFFT_Imag256();

    /* Clear frame work buffers before filling valid samples. */
    for (i = 0u; i < MAX4466_FRAME_SAMPLES; i++)
    {
        fft_real[i] = 0;
        fft_imag[i] = 0;
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
        int32_t centered_mv = (centered_count * 3300) / 4095;
        sum_sq += (uint64_t)((int64_t)centered_mv * (int64_t)centered_mv);
        fft_real[i] = SignalFFT_MulQ15(centered_mv, window_q15[i]);
    }

    /* RMS of AC component in mV domain. */
    feature->rms = MAX4466_Isqrt64(sum_sq / used_count);

    SignalFFT_Run256(fft_real, fft_imag);

    /*
     * Frequency resolution = Fs/N = 8000/256 = 31.25 Hz per bin.
     * Band partition:
     *   low : bin  4..15  (~125..468 Hz)
     *   mid : bin 16..47  (~500..1468 Hz)
     *   high: bin 48..127 (~1500..3968 Hz)
     * Dominant frequency is the strongest bin in 1..127.
     */
    for (i = 1u; i < (MAX4466_FRAME_SAMPLES / 2u); i++)
    {
        uint64_t magnitude_sq = (uint64_t)((int64_t)fft_real[i] * (int64_t)fft_real[i]) +
                                (uint64_t)((int64_t)fft_imag[i] * (int64_t)fft_imag[i]);

        if ((i >= 4u) && (i <= 15u))
        {
            low_energy += magnitude_sq;
        }
        else if ((i >= 16u) && (i <= 47u))
        {
            mid_energy += magnitude_sq;
        }
        else if (i >= 48u)
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
    total_energy_64 = low_energy + mid_energy + high_energy;
    total_energy = MAX4466_ClampU64ToU32(total_energy_64);
    feature->total_energy = total_energy;
    if (total_energy_64 != 0u)
    {
        /* Ratio used by upper-layer decision logic to suppress low-frequency hum/noise. */
        feature->band_ratio_pct = (uint16_t)(((mid_energy + high_energy) * 100u) / total_energy_64);
        feature->high_ratio_pct = (uint16_t)((high_energy * 100u) / total_energy_64);
    }

    /* Convert dominant bin to frequency in Hz. */
    feature->dominant_freq_hz = (uint16_t)((dominant_bin * MAX4466_SAMPLE_RATE_HZ) / MAX4466_FRAME_SAMPLES);
}
