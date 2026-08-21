#include "./Hardware/MAX9814/MAX9814.h"

#include <string.h>

#include "arm_common_tables.h"
#include "arm_math.h"

#define MAX9814_SAMPLE_RATE_HZ 4000u
#define MAX9814_DMA_BUFFER_SIZE 128u
#define MAX9814_WINDOW_SIZE 128u
#define MAX9814_WINDOW_HOP 64u
#define MAX9814_UNIQUE_BINS 64u
#define MAX9814_TIM_PRESCALER 179u
#define MAX9814_TIM_PERIOD 99u
#define MAX9814_TIM_COMPARE 49u
#define MAX9814_TIM_CHANNEL TIM_CHANNEL_3
#define MAX9814_ADC_TRIGGER ADC_EXTERNALTRIGCONV_T1_CC3
#define MAX9814_BIN_ENERGY_SHIFT 8u
#define MAX9814_ANALYSIS_MIN_SPAN 128u
#define MAX9814_FLOOR_RISE_SHIFT 7u
#define MAX9814_FLOOR_FALL_SHIFT 3u
#define MAX9814_PEAK_DECAY_SHIFT 6u
#define MAX9814_TRANSIENT_BOOST_LIMIT_PERCENT 15u
#define MAX9814_DMA_STALL_TIMEOUT_MS 80u
#define MAX9814_SOFTWARE_CAPTURE_INTERVAL_MS 1u
#define MAX9814_SOFTWARE_CAPTURE_SAMPLES 4u
#define MAX9814_ADC_POLL_TIMEOUT_MS 2u

typedef struct
{
    uint32_t floor;
    uint32_t peak;
    uint16_t level;
} MAX9814_Normalizer_t;

typedef enum
{
    MAX9814_SAMPLING_DMA = 0u,
    MAX9814_SAMPLING_SOFTWARE = 1u
} MAX9814_SamplingMode_t;

static ADC_HandleTypeDef hadc1_max9814;
static DMA_HandleTypeDef hdma_adc1_max9814;
static TIM_HandleTypeDef htim1_max9814;
static const arm_cfft_instance_q15 s_cfft_q15 = {
    MAX9814_WINDOW_SIZE,
    twiddleCoef_128_q15,
    armBitRevIndexTable_fixed_128,
    ARMBITREVINDEXTABLE_FIXED_128_TABLE_LENGTH
};

static volatile uint8_t s_pending_halves = 0u;
static uint8_t s_max9814_inited = 0u;
static uint8_t s_has_full_window = 0u;
static uint16_t s_dma_buffer[MAX9814_DMA_BUFFER_SIZE];
static uint16_t s_sample_history[MAX9814_WINDOW_SIZE];
static uint16_t s_window_samples[MAX9814_WINDOW_SIZE];
static uint16_t s_history_write_index = 0u;
static uint16_t s_history_count = 0u;
static uint16_t s_samples_since_analysis = 0u;
static uint16_t s_last_raw_pp = 0u;
static uint16_t s_music_level_permille = 0u;
static uint16_t s_activity_level_permille = 0u;
static uint32_t s_last_activity_energy = 0u;
static uint32_t s_last_dma_tick = 0u;
static uint32_t s_last_software_capture_tick = 0u;
static MAX9814_SamplingMode_t s_sampling_mode = MAX9814_SAMPLING_DMA;
static MAX9814_Normalizer_t s_music_normalizer = {0u, 1u, 0u};
static MAX9814_Normalizer_t s_activity_normalizer = {0u, 1u, 0u};
static q15_t s_fft_buffer[MAX9814_WINDOW_SIZE * 2u];

static const q15_t s_hann_window[MAX9814_WINDOW_SIZE] = {
    0, 20, 80, 180, 320, 499, 717, 973,
    1267, 1597, 1965, 2367, 2803, 3273, 3775, 4308,
    4870, 5461, 6078, 6721, 7387, 8075, 8784, 9511,
    10254, 11013, 11785, 12569, 13361, 14161, 14967, 15776,
    16586, 17396, 18203, 19006, 19803, 20591, 21369, 22135,
    22886, 23622, 24340, 25039, 25716, 26371, 27001, 27605,
    28181, 28729, 29247, 29733, 30186, 30606, 30990, 31340,
    31652, 31927, 32164, 32363, 32522, 32642, 32722, 32762,
    32762, 32722, 32642, 32522, 32363, 32164, 31927, 31652,
    31340, 30990, 30606, 30186, 29733, 29247, 28729, 28181,
    27605, 27001, 26371, 25716, 25039, 24340, 23622, 22886,
    22135, 21369, 20591, 19803, 19006, 18203, 17396, 16586,
    15776, 14967, 14161, 13361, 12569, 11785, 11013, 10254,
    9511, 8784, 8075, 7387, 6721, 6078, 5461, 4870,
    4308, 3775, 3273, 2803, 2367, 1965, 1597, 1267,
    973, 717, 499, 320, 180, 80, 20, 0
};

static q15_t MAX9814_ClampQ15(int32_t value)
{
    if (value > 32767)
    {
        return 32767;
    }
    if (value < -32768)
    {
        return -32768;
    }
    return (q15_t)value;
}

static uint16_t MAX9814_ApplyAttackRelease(uint16_t current, uint16_t target)
{
    uint16_t delta;
    uint16_t step;

    if (target > current)
    {
        delta = (uint16_t)(target - current);
        step = (uint16_t)(((uint32_t)delta * 3u + 3u) / 4u);
        if (step == 0u)
        {
            step = 1u;
        }
        return (uint16_t)(current + step);
    }

    if (target < current)
    {
        delta = (uint16_t)(current - target);
        step = (uint16_t)((delta + 7u) / 8u);
        if (step == 0u)
        {
            step = 1u;
        }
        return (uint16_t)(current - step);
    }

    return current;
}

static uint16_t MAX9814_NormalizeLevel(uint32_t raw_value, MAX9814_Normalizer_t *normalizer)
{
    uint32_t signal;
    uint32_t range;
    uint32_t decay;
    uint32_t target;

    if (raw_value >= normalizer->floor)
    {
        normalizer->floor += (raw_value - normalizer->floor) >> MAX9814_FLOOR_RISE_SHIFT;
    }
    else
    {
        normalizer->floor -= (normalizer->floor - raw_value) >> MAX9814_FLOOR_FALL_SHIFT;
    }

    if (raw_value > normalizer->floor)
    {
        signal = raw_value - normalizer->floor;
    }
    else
    {
        signal = 0u;
    }

    if (signal >= normalizer->peak)
    {
        normalizer->peak = signal;
    }
    else
    {
        decay = (normalizer->peak >> MAX9814_PEAK_DECAY_SHIFT) + 1u;
        if (normalizer->peak > decay)
        {
            normalizer->peak -= decay;
        }
        else
        {
            normalizer->peak = 1u;
        }

        if (signal > normalizer->peak)
        {
            normalizer->peak = signal;
        }
    }

    range = normalizer->peak;
    if (range < MAX9814_ANALYSIS_MIN_SPAN)
    {
        range = MAX9814_ANALYSIS_MIN_SPAN;
    }

    target = (signal * 1000u) / range;
    if (target > 1000u)
    {
        target = 1000u;
    }

    normalizer->level = MAX9814_ApplyAttackRelease(normalizer->level, (uint16_t)target);
    return normalizer->level;
}

static uint8_t MAX9814_AcquirePendingHalf(uint16_t *start_index)
{
    uint32_t primask = __get_PRIMASK();
    uint8_t pending;

    __disable_irq();
    pending = s_pending_halves;

    if ((pending & 0x01u) != 0u)
    {
        s_pending_halves &= (uint8_t)~0x01u;
        *start_index = 0u;
        if (primask == 0u)
        {
            __enable_irq();
        }
        return 1u;
    }

    if ((pending & 0x02u) != 0u)
    {
        s_pending_halves &= (uint8_t)~0x02u;
        *start_index = MAX9814_WINDOW_HOP;
        if (primask == 0u)
        {
            __enable_irq();
        }
        return 1u;
    }

    if (primask == 0u)
    {
        __enable_irq();
    }
    return 0u;
}

static void MAX9814_RunAnalysis(void)
{
    uint32_t sample_sum = 0u;
    uint16_t sample_min = 0x0FFFu;
    uint16_t sample_max = 0u;
    int32_t mean_value;
    uint32_t index;
    uint64_t low_band = 0u;
    uint64_t mid_band = 0u;
    uint64_t high_band = 0u;
    uint64_t activity_band = 0u;
    uint64_t weighted_music;
    uint64_t activity_delta = 0u;
    uint64_t transient_boost;

    for (index = 0u; index < MAX9814_WINDOW_SIZE; index++)
    {
        uint16_t sample = s_window_samples[index];
        sample_sum += sample;
        if (sample < sample_min)
        {
            sample_min = sample;
        }
        if (sample > sample_max)
        {
            sample_max = sample;
        }
    }

    s_last_raw_pp = (sample_max >= sample_min) ? (uint16_t)(sample_max - sample_min) : 0u;
    mean_value = (int32_t)(sample_sum / MAX9814_WINDOW_SIZE);

    for (index = 0u; index < MAX9814_WINDOW_SIZE; index++)
    {
        int32_t centered_sample = (int32_t)s_window_samples[index] - mean_value;
        int32_t q15_sample = centered_sample << 4;
        q15_t windowed_sample = (q15_t)(((int32_t)MAX9814_ClampQ15(q15_sample) * s_hann_window[index]) >> 15);

        s_fft_buffer[index * 2u] = windowed_sample;
        s_fft_buffer[(index * 2u) + 1u] = 0;
    }

    arm_cfft_q15(&s_cfft_q15, s_fft_buffer, 0u, 1u);

    for (index = 1u; index < MAX9814_UNIQUE_BINS; index++)
    {
        int32_t real_part = (int32_t)s_fft_buffer[index * 2u];
        int32_t imag_part = (int32_t)s_fft_buffer[(index * 2u) + 1u];
        uint32_t magnitude = (uint32_t)((((uint64_t)(real_part * real_part)) +
                                         ((uint64_t)(imag_part * imag_part))) >>
                                        MAX9814_BIN_ENERGY_SHIFT);

        if ((magnitude == 0u) && ((real_part != 0) || (imag_part != 0)))
        {
            magnitude = 1u;
        }

        activity_band += magnitude;

        if ((index >= 2u) && (index <= 7u))
        {
            low_band += magnitude;
        }
        else if ((index >= 8u) && (index <= 32u))
        {
            mid_band += magnitude;
        }
        else if ((index >= 33u) && (index <= 63u))
        {
            high_band += magnitude;
        }
    }

    weighted_music = (low_band * 4u) + (mid_band * 4u) + (high_band * 2u);
    if (activity_band > s_last_activity_energy)
    {
        activity_delta = activity_band - s_last_activity_energy;
    }

    transient_boost = activity_delta / 3u;
    if (transient_boost > ((weighted_music * MAX9814_TRANSIENT_BOOST_LIMIT_PERCENT) / 100u))
    {
        transient_boost = (weighted_music * MAX9814_TRANSIENT_BOOST_LIMIT_PERCENT) / 100u;
    }
    weighted_music += transient_boost;
    s_last_activity_energy = (activity_band > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)activity_band;

    s_music_level_permille = MAX9814_NormalizeLevel((weighted_music > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)weighted_music,
                                                    &s_music_normalizer);
    s_activity_level_permille = MAX9814_NormalizeLevel((activity_band > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)activity_band,
                                                       &s_activity_normalizer);
}

static void MAX9814_CopyHistoryToWindow(void)
{
    uint16_t history_index = s_history_write_index;
    uint16_t window_index;

    for (window_index = 0u; window_index < MAX9814_WINDOW_SIZE; window_index++)
    {
        s_window_samples[window_index] = s_sample_history[history_index];
        history_index++;
        if (history_index >= MAX9814_WINDOW_SIZE)
        {
            history_index = 0u;
        }
    }
}

static void MAX9814_PushBlock(const uint16_t *samples, uint16_t sample_count)
{
    uint16_t sample_index;

    for (sample_index = 0u; sample_index < sample_count; sample_index++)
    {
        s_sample_history[s_history_write_index] = samples[sample_index];
        s_history_write_index++;
        if (s_history_write_index >= MAX9814_WINDOW_SIZE)
        {
            s_history_write_index = 0u;
        }

        if (s_history_count < MAX9814_WINDOW_SIZE)
        {
            s_history_count++;
        }

        if (s_history_count < MAX9814_WINDOW_SIZE)
        {
            continue;
        }

        if (s_has_full_window == 0u)
        {
            s_has_full_window = 1u;
            s_samples_since_analysis = 0u;
            MAX9814_CopyHistoryToWindow();
            MAX9814_RunAnalysis();
            continue;
        }

        s_samples_since_analysis++;
        if (s_samples_since_analysis >= MAX9814_WINDOW_HOP)
        {
            s_samples_since_analysis = 0u;
            MAX9814_CopyHistoryToWindow();
            MAX9814_RunAnalysis();
        }
    }
}

static void MAX9814_ADC_Reconfigure(uint32_t external_trigger)
{
    ADC_ChannelConfTypeDef sConfig = {0};

    hadc1_max9814.Instance = MAX9814_ADC_INSTANCE;
    hadc1_max9814.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc1_max9814.Init.ContinuousConvMode = DISABLE;
    hadc1_max9814.Init.DiscontinuousConvMode = DISABLE;
    hadc1_max9814.Init.ExternalTrigConv = external_trigger;
    hadc1_max9814.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1_max9814.Init.NbrOfConversion = 1;
    (void)HAL_ADC_Init(&hadc1_max9814);

    sConfig.Channel = MAX9814_ADC_CHANNEL;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_71CYCLES_5;
    (void)HAL_ADC_ConfigChannel(&hadc1_max9814, &sConfig);
    (void)HAL_ADCEx_Calibration_Start(&hadc1_max9814);
}

static uint16_t MAX9814_ReadSoftwareSample(void)
{
    uint16_t sample = 2048u;

    if (HAL_ADC_Start(&hadc1_max9814) == HAL_OK)
    {
        if (HAL_ADC_PollForConversion(&hadc1_max9814, MAX9814_ADC_POLL_TIMEOUT_MS) == HAL_OK)
        {
            sample = (uint16_t)HAL_ADC_GetValue(&hadc1_max9814);
        }
        (void)HAL_ADC_Stop(&hadc1_max9814);
    }

    return sample;
}

static void MAX9814_SwitchToSoftwareSampling(void)
{
    (void)HAL_TIM_OC_Stop(&htim1_max9814, MAX9814_TIM_CHANNEL);
    (void)HAL_ADC_Stop_DMA(&hadc1_max9814);
    (void)HAL_ADC_DeInit(&hadc1_max9814);
    MAX9814_ADC_Reconfigure(ADC_SOFTWARE_START);

    s_pending_halves = 0u;
    s_sampling_mode = MAX9814_SAMPLING_SOFTWARE;
    s_last_software_capture_tick = HAL_GetTick();
}

static void MAX9814_CaptureSoftwareSamples(void)
{
    uint16_t samples[MAX9814_SOFTWARE_CAPTURE_SAMPLES];
    uint16_t sample_index;
    uint16_t sample_min = 0x0FFFu;
    uint16_t sample_max = 0u;

    for (sample_index = 0u; sample_index < MAX9814_SOFTWARE_CAPTURE_SAMPLES; sample_index++)
    {
        uint16_t sample = MAX9814_ReadSoftwareSample();

        samples[sample_index] = sample;
        if (sample < sample_min)
        {
            sample_min = sample;
        }
        if (sample > sample_max)
        {
            sample_max = sample;
        }
    }

    s_last_raw_pp = (sample_max >= sample_min) ? (uint16_t)(sample_max - sample_min) : 0u;
    MAX9814_PushBlock(samples, MAX9814_SOFTWARE_CAPTURE_SAMPLES);
}

static void MAX9814_Service(void)
{
    uint32_t now_tick;
    uint16_t start_index;

    if (s_max9814_inited == 0u)
    {
        return;
    }

    now_tick = HAL_GetTick();
    if ((s_sampling_mode == MAX9814_SAMPLING_DMA) &&
        ((now_tick - s_last_dma_tick) >= MAX9814_DMA_STALL_TIMEOUT_MS))
    {
        MAX9814_SwitchToSoftwareSampling();
    }

    while (MAX9814_AcquirePendingHalf(&start_index) != 0u)
    {
        MAX9814_PushBlock(&s_dma_buffer[start_index], MAX9814_WINDOW_HOP);
    }

    if ((s_sampling_mode == MAX9814_SAMPLING_SOFTWARE) &&
        ((now_tick - s_last_software_capture_tick) >= MAX9814_SOFTWARE_CAPTURE_INTERVAL_MS))
    {
        s_last_software_capture_tick = now_tick;
        MAX9814_CaptureSoftwareSamples();
    }
}

static void MAX9814_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitStruct.Pin = MAX9814_GAIN_PIN | MAX9814_AR_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(MAX9814_GAIN_PORT, &GPIO_InitStruct);

    HAL_GPIO_WritePin(MAX9814_GAIN_PORT, MAX9814_GAIN_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MAX9814_AR_PORT, MAX9814_AR_PIN, GPIO_PIN_RESET);

    GPIO_InitStruct.Pin = MAX9814_OUT_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    HAL_GPIO_Init(MAX9814_OUT_PORT, &GPIO_InitStruct);
}

static void MAX9814_TIM1_Init(void)
{
    TIM_OC_InitTypeDef sConfigOC = {0};

    htim1_max9814.Instance = TIM1;
    htim1_max9814.Init.Prescaler = MAX9814_TIM_PRESCALER;
    htim1_max9814.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim1_max9814.Init.Period = MAX9814_TIM_PERIOD;
    htim1_max9814.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim1_max9814.Init.RepetitionCounter = 0u;
    htim1_max9814.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    (void)HAL_TIM_OC_Init(&htim1_max9814);

    sConfigOC.OCMode = TIM_OCMODE_TIMING;
    sConfigOC.Pulse = MAX9814_TIM_COMPARE;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    (void)HAL_TIM_OC_ConfigChannel(&htim1_max9814, &sConfigOC, MAX9814_TIM_CHANNEL);
}

static void MAX9814_ADC_Init(void)
{
    __HAL_RCC_ADC_CONFIG(MAX9814_ADC_CLOCK_DIV);
    MAX9814_ADC_Reconfigure(MAX9814_ADC_TRIGGER);
}

void HAL_ADC_MspInit(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1)
    {
        __HAL_RCC_ADC1_CLK_ENABLE();
        __HAL_RCC_DMA1_CLK_ENABLE();

        hdma_adc1_max9814.Instance = DMA1_Channel1;
        hdma_adc1_max9814.Init.Direction = DMA_PERIPH_TO_MEMORY;
        hdma_adc1_max9814.Init.PeriphInc = DMA_PINC_DISABLE;
        hdma_adc1_max9814.Init.MemInc = DMA_MINC_ENABLE;
        hdma_adc1_max9814.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
        hdma_adc1_max9814.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
        hdma_adc1_max9814.Init.Mode = DMA_CIRCULAR;
        hdma_adc1_max9814.Init.Priority = DMA_PRIORITY_HIGH;
        (void)HAL_DMA_Init(&hdma_adc1_max9814);

        __HAL_LINKDMA(hadc, DMA_Handle, hdma_adc1_max9814);

        HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 2u, 0u);
        HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);
    }
}

void HAL_TIM_OC_MspInit(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM1)
    {
        __HAL_RCC_TIM1_CLK_ENABLE();
    }
}

void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1)
    {
        s_last_dma_tick = HAL_GetTick();
        s_pending_halves |= 0x01u;
    }
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1)
    {
        s_last_dma_tick = HAL_GetTick();
        s_pending_halves |= 0x02u;
    }
}

void MAX9814_Init(void)
{
    memset(s_dma_buffer, 0, sizeof(s_dma_buffer));
    memset(s_sample_history, 0, sizeof(s_sample_history));
    memset(s_window_samples, 0, sizeof(s_window_samples));
    memset(s_fft_buffer, 0, sizeof(s_fft_buffer));

    s_pending_halves = 0u;
    s_has_full_window = 0u;
    s_history_write_index = 0u;
    s_history_count = 0u;
    s_samples_since_analysis = 0u;
    s_last_raw_pp = 0u;
    s_music_level_permille = 0u;
    s_activity_level_permille = 0u;
    s_last_activity_energy = 0u;
    s_last_dma_tick = HAL_GetTick();
    s_last_software_capture_tick = 0u;
    s_sampling_mode = MAX9814_SAMPLING_DMA;
    s_music_normalizer.floor = 0u;
    s_music_normalizer.peak = 1u;
    s_music_normalizer.level = 0u;
    s_activity_normalizer.floor = 0u;
    s_activity_normalizer.peak = 1u;
    s_activity_normalizer.level = 0u;

    MAX9814_GPIO_Init();
    MAX9814_TIM1_Init();
    MAX9814_ADC_Init();
    (void)HAL_ADC_Start_DMA(&hadc1_max9814, (uint32_t *)s_dma_buffer, MAX9814_DMA_BUFFER_SIZE);
    (void)HAL_TIM_OC_Start(&htim1_max9814, MAX9814_TIM_CHANNEL);
    s_max9814_inited = 1u;
}

void MAX9814_Task(void)
{
    MAX9814_Service();
}

uint16_t MAX9814_ReadRaw(void)
{
    return s_last_raw_pp;
}

uint16_t MAX9814_ReadMusicLevelPermille(void)
{
    return s_music_level_permille;
}

uint16_t MAX9814_ReadActivityLevelPermille(void)
{
    return s_activity_level_permille;
}

void MAX9814_DMA_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_adc1_max9814);
}
