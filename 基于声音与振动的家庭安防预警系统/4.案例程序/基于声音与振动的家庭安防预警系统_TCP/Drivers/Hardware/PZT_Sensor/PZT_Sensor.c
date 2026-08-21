#include ".\Hardware\PZT_Sensor\PZT_Sensor.h"
#include ".\Hardware\SignalFFT\SignalFFT.h"

#include <string.h>

void PZT_Sensor_Init(void)
{
    SignalFFT_Init256();
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
    const int16_t *window_q15;
    int16_t *fft_real;
    int16_t *fft_imag;

    if ((samples == NULL) || (feature == NULL))
    {
        return;
    }

    SignalFFT_Init256();
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

    window_q15 = SignalFFT_Window256();
    fft_real = SignalFFT_Real256();
    fft_imag = SignalFFT_Imag256();

    /* Reset FFT work buffers. */
    for (i = 0u; i < PZT_FRAME_SAMPLES; i++)
    {
        fft_real[i] = 0;
        fft_imag[i] = 0;
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
        int32_t centered_mv = sensor_mv - mean_sensor_mv;
        int32_t sample_sign = 0;
        int32_t abs_mv = 0;

        abs_mv = (centered_mv >= 0) ? centered_mv : -centered_mv;
        if ((uint32_t)abs_mv > feature->peak_mv)
        {
            feature->peak_mv = (uint32_t)abs_mv;
        }

        sum_sq += (uint64_t)((int64_t)centered_mv * (int64_t)centered_mv);
        fft_real[i] = SignalFFT_MulQ15(centered_mv, window_q15[i]);

        if (centered_mv > 0)
        {
            sample_sign = 1;
        }
        else if (centered_mv < 0)
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

    SignalFFT_Run256(fft_real, fft_imag);

    /*
     * Search the full measurable band. With Fs/N = 31.25 Hz/bin and Fs=8 kHz,
     * bins 1..127 cover about 31..3968 Hz, matching the 0..4 kHz UI range.
     */
    for (i = 1u; i < (PZT_FRAME_SAMPLES / 2u); i++)
    {
        uint64_t magnitude_sq = (uint64_t)((int64_t)fft_real[i] * (int64_t)fft_real[i]) +
                                (uint64_t)((int64_t)fft_imag[i] * (int64_t)fft_imag[i]);
        if (magnitude_sq > peak_energy)
        {
            peak_energy = magnitude_sq;
            dominant_bin = i;
        }
    }

    /* Convert dominant bin index to frequency in Hz. */
    feature->dominant_freq_hz = (uint16_t)((dominant_bin * PZT_SAMPLE_RATE_HZ) / PZT_FRAME_SAMPLES);
}
