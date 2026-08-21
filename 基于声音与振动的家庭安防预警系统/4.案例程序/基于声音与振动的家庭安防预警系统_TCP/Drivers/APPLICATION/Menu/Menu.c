#include ".\Application\Menu\Menu.h"
#include ".\Application\Menu\MenuFramework.h"
#include ".\Application\TcpProtocol\TcpProtocol.h"
#include ".\Hardware\Buzzer\Buzzer.h"
#include ".\Hardware\ESP_01S\ESP_01S.h"
#include ".\Hardware\KEY\KEY.h"
#include ".\Hardware\OLED\OLED.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SECURITY_LED_PORT GPIOA
#define SECURITY_LED_PIN GPIO_PIN_6
#define SECURITY_LED_CLK_ENABLE() __HAL_RCC_GPIOA_CLK_ENABLE()

#define SECURITY_ADC_CHANNEL_COUNT 2u
#define SECURITY_DMA_FRAME_WORDS (MAX4466_FRAME_SAMPLES * SECURITY_ADC_CHANNEL_COUNT)
#define SECURITY_DMA_BUFFER_WORDS (SECURITY_DMA_FRAME_WORDS * 2u)
#define SECURITY_WARMUP_MS 3000u
#define SECURITY_MENU_REFRESH_MS 50u
#define SECURITY_ESP_RETRY_MS 5000u
#define SECURITY_SUSPICIOUS_BLINK_MS 500u
#define SECURITY_ALARM_BLINK_MS 125u
/* Floors keep thresholds robust when ambient baseline energy is very low. */
#define SECURITY_AUDIO_MEDIUM_ENERGY_FLOOR 1500u
#define SECURITY_AUDIO_STRONG_ENERGY_FLOOR 6000u
#define SECURITY_VIBRATION_ENERGY_FLOOR 200u
#define SECURITY_VIBRATION_STRONG_ENERGY_FLOOR 800u
#define SECURITY_FREQUENCY_SCORE_MAX_HZ 4000u
#define SECURITY_VIBRATION_FREQ_MIN_HZ 31u
#define SECURITY_VIBRATION_FREQ_MAX_HZ SECURITY_FREQUENCY_SCORE_MAX_HZ
#define SECURITY_TCP_DEVICE_ID "HOME_SECURITY_001"
#define SECURITY_TCP_CLIENT_TIMEOUT_MS 60000u
#define SECURITY_TCP_LOGIN_TIMEOUT_MS 10000u
#define SECURITY_TCP_PUSH_INTERVAL_MS 1000u
#define SECURITY_TCP_REPORT_INTERVAL_MS 1500u
#define SECURITY_TCP_IDLE_REPORT_INTERVAL_MS 3000u
#define SECURITY_TCP_REPORT_SCORE_DELTA 8u
#define SECURITY_ALARM_REPORT_MIN_INTERVAL_MS 1200u
#define SECURITY_MAX_FRAMES_PER_TASK 1u
#define SECURITY_STATUS_RENDER_MIN_MS 500u
#define SECURITY_AUDIO_MEDIUM_SCORE 55u
#define SECURITY_AUDIO_STRONG_SCORE 78u
#define SECURITY_VIBRATION_MEDIUM_SCORE 50u
#define SECURITY_VIBRATION_STRONG_SCORE 75u
#define SECURITY_VOTE_WINDOW_BITS 0x07u
#define SECURITY_VOTE_MEDIUM_COUNT 2u
#define SECURITY_VOTE_STRONG_COUNT 2u
#define SECURITY_TCP_TX_FAIL_LIMIT 4u
#define SECURITY_TCP_TX_BACKOFF_MS 500u
#define SECURITY_TCP_TX_TIMEOUT_BACKOFF_MS 1500u
#define SECURITY_TCP_TX_GAP_MS 180u
#define SECURITY_TCP_MAINTAIN_SEND_BUDGET 1u
#define SECURITY_TCP_NO_ACTIVE_LINK 0xFFu
#define SECURITY_ALARM_HISTORY_SIZE 16u
#define SECURITY_TCP_TX_BODY_SIZE 1024u
#define SECURITY_PARAM_HOLD_MIN 3000
#define SECURITY_PARAM_HOLD_MAX 60000
#define SECURITY_PARAM_FUSION_MIN 100
#define SECURITY_PARAM_FUSION_MAX 1000
#define SECURITY_PARAM_AUDIO_MEDIUM_MIN 20
#define SECURITY_PARAM_AUDIO_MEDIUM_MAX 90
#define SECURITY_PARAM_AUDIO_STRONG_MIN 30
#define SECURITY_PARAM_AUDIO_STRONG_MAX 95

typedef enum
{
    SIGNAL_LEVEL_NONE = 0,
    SIGNAL_LEVEL_MEDIUM,
    SIGNAL_LEVEL_STRONG
} SignalLevel_t;

typedef enum
{
    UI_PAGE_MENU = 0,
    UI_PAGE_STATUS,
    UI_PAGE_WIFI
} SecurityUiPage_t;

typedef struct
{
    uint8_t logged_in;
    uint8_t tx_fail_count;
    uint32_t last_seen_tick;
    uint32_t tx_blocked_until_tick;
    uint32_t last_tx_tick;
    uint32_t tx_ok_count;
    uint16_t last_rx_type;
    uint16_t last_tx_type;
    uint8_t last_tx_status;
} SecurityTcpClient_t;

typedef struct
{
    uint32_t tick;
    uint32_t audio_energy;
    uint32_t vibration_peak_mv;
    uint32_t vibration_energy;
    uint16_t audio_freq_hz;
    uint16_t audio_ratio_pct;
    uint16_t vibration_freq_hz;
    uint8_t state;
    char reason[20];
} SecurityAlarmRecord_t;

typedef struct
{
    uint8_t medium_history;
    uint8_t strong_history;
    uint8_t score;
} SecurityVoteState_t;

static ADC_HandleTypeDef s_hadc1;
static DMA_HandleTypeDef s_hdma_adc1;
static TIM_HandleTypeDef s_htim3;
static volatile uint16_t s_adc_dma_buffer[SECURITY_DMA_BUFFER_WORDS];
static volatile uint8_t s_adc_ready_flags = 0u;
static uint16_t s_audio_frame[MAX4466_FRAME_SAMPLES];
static uint16_t s_vibration_frame[PZT_FRAME_SAMPLES];
static SecuritySystem_Status_t s_status;
static SecurityUiPage_t s_ui_page = UI_PAGE_MENU;
static uint32_t s_warmup_start_tick = 0u;
static uint32_t s_last_audio_medium_tick = 0u;
static uint32_t s_last_vibration_medium_tick = 0u;
static uint32_t s_last_menu_render_tick = 0u;
static uint32_t s_last_esp_retry_tick = 0u;
static uint8_t s_force_render = 1u;
static uint8_t s_baseline_ready = 0u;
static MF_Menu_t *s_root_menu = NULL;
static MF_Menu_t *s_param_menu = NULL;
static uint8_t s_armed_toggle = 1u;
static int32_t s_alarm_hold_setting = 15000;
static int32_t s_fusion_window_setting = 300;
static int32_t s_audio_medium_ratio_setting = 40;
static int32_t s_audio_strong_ratio_setting = 55;
static SecurityTcpClient_t s_tcp_clients[TCP_PROTOCOL_MAX_LINKS];
static uint8_t s_tcp_active_link = SECURITY_TCP_NO_ACTIVE_LINK;
static SecurityAlarmRecord_t s_alarm_history[SECURITY_ALARM_HISTORY_SIZE];
static uint8_t s_alarm_history_head = 0u;
static uint8_t s_alarm_history_count = 0u;
static uint8_t s_last_link_mask = 0u;
static uint32_t s_last_status_push_tick = 0u;
static uint32_t s_last_audio_report_tick = 0u;
static uint32_t s_last_vibration_report_tick = 0u;
static uint32_t s_last_alarm_report_tick = 0u;
static uint8_t s_status_push_pending = 0u;
static uint8_t s_audio_report_pending = 0u;
static uint8_t s_vibration_report_pending = 0u;
static uint8_t s_alarm_report_pending = 0u;
static uint32_t s_pending_audio_report_tick = 0u;
static uint32_t s_pending_vibration_report_tick = 0u;
static SignalLevel_t s_pending_audio_report_level = SIGNAL_LEVEL_NONE;
static SignalLevel_t s_pending_vibration_report_level = SIGNAL_LEVEL_NONE;
static uint8_t s_last_audio_report_score = 0u;
static uint8_t s_last_vibration_report_score = 0u;
static uint8_t s_last_audio_report_detect_score = 0u;
static uint8_t s_last_vibration_report_detect_score = 0u;
static SignalLevel_t s_last_audio_report_level = SIGNAL_LEVEL_NONE;
static SignalLevel_t s_last_vibration_report_level = SIGNAL_LEVEL_NONE;
static uint32_t s_tcp_seq = 1u;
static char s_tcp_body[SECURITY_TCP_TX_BODY_SIZE];
static char s_line_buffer[64];
static SecurityVoteState_t s_audio_vote;
static SecurityVoteState_t s_vibration_vote;

static void SecuritySystem_LED_Init(void);
static void SecuritySystem_LED_Write(uint8_t on);
static void SecuritySystem_SetState(SecuritySystem_State_t state);
static uint8_t SecuritySystem_HasRecentAudioEvent(uint32_t now_tick);
static uint8_t SecuritySystem_HasRecentVibrationEvent(uint32_t now_tick);
static void SecuritySystem_ResetFusionHistory(void);
static void SecuritySystem_SetLastReason(const char *reason);
static void SecuritySystem_SetArmed(uint8_t armed);
static void SecuritySystem_ClearAlarm(void);
static void SecuritySystem_TriggerAlarm(const char *reason);
static void SecuritySystem_UpdateBaselines(void);
static uint8_t SecuritySystem_RatioScore(uint32_t value, uint32_t medium, uint32_t strong);
static uint8_t SecuritySystem_ClampScoreU16(uint16_t value, uint16_t medium, uint16_t strong);
static uint8_t SecuritySystem_FrequencyScore(uint16_t freq_hz);
static SignalLevel_t SecuritySystem_UpdateVote(SecurityVoteState_t *vote, uint8_t score, uint8_t medium_threshold, uint8_t strong_threshold);
static void SecuritySystem_ResetVotes(void);
static SignalLevel_t SecuritySystem_ClassifyAudio(void);
static SignalLevel_t SecuritySystem_ClassifyVibration(void);
static void SecuritySystem_ProcessDetection(SignalLevel_t audio_level, SignalLevel_t vibration_level);
static void SecuritySystem_ProcessFrame(const volatile uint16_t *frame_data);
static void SecuritySystem_ProcessPendingFrames(void);
static void SecuritySystem_UpdateOutputs(void);
static void SecuritySystem_MaintainState(void);
static void SecuritySystem_HandleEspPacket(ESP_DataPacket_t *packet);
static void SecuritySystem_HandleEspLinkEvent(uint8_t link_id, ESP_LinkEvent_t event);
static void SecuritySystem_HandleTcpMessage(const TcpProtocol_Message_t *message);
static void SecuritySystem_HandleTcpParseError(uint8_t link_id, uint32_t seq, TcpProtocol_ErrorCode_t error);
static uint8_t SecuritySystem_SendTcpFrame(uint8_t link_id, TcpProtocol_MessageType_t type, uint32_t seq, const char *json);
static uint8_t SecuritySystem_SendBytesChunked(uint8_t link_id, const uint8_t *data, uint32_t len);
static uint8_t SecuritySystem_CanSendTcp(uint8_t link_id);
static void SecuritySystem_RecordTcpSendResult(uint8_t link_id, uint8_t ok);
static uint8_t SecuritySystem_QueuedTcpSendReady(void);
static void SecuritySystem_SendError(uint8_t link_id, uint32_t seq, TcpProtocol_ErrorCode_t error, const char *detail);
static uint8_t SecuritySystem_BuildStatusJson(char *buffer, uint16_t size);
static void SecuritySystem_SendStatus(uint8_t link_id, TcpProtocol_MessageType_t type, uint32_t seq);
static void SecuritySystem_BroadcastStatus(void);
static uint8_t SecuritySystem_SendPendingStatus(void);
static void SecuritySystem_BroadcastAudioReport(uint32_t tick, SignalLevel_t level);
static void SecuritySystem_BroadcastVibrationReport(uint32_t tick, SignalLevel_t level);
static uint32_t SecuritySystem_ReportIntervalForLevel(SignalLevel_t level);
static void SecuritySystem_BroadcastAlarmReport(void);
static uint8_t SecuritySystem_SendPendingAudioReport(void);
static uint8_t SecuritySystem_SendPendingVibrationReport(void);
static uint8_t SecuritySystem_SendPendingAlarmReport(void);
static void SecuritySystem_FlushPendingTcpReports(void);
static void SecuritySystem_DropRealtimePendingAfterTxFailure(TcpProtocol_MessageType_t type);
static void SecuritySystem_RecordAlarm(void);
static void SecuritySystem_SendHistory(uint8_t link_id, uint32_t seq);
static uint16_t SecuritySystem_FormatHistoryItem(const SecurityAlarmRecord_t *record, uint8_t first_item, char *buffer, uint16_t size);
static void SecuritySystem_MaintainTcpClients(void);
static void SecuritySystem_CloseTcpClient(uint8_t link_id);
static void SecuritySystem_ResetTcpClient(uint8_t link_id);
static void SecuritySystem_SelectTcpClient(uint8_t link_id);
static uint8_t SecuritySystem_IsLinkActive(uint8_t link_id);
static uint8_t SecuritySystem_IsLoggedIn(uint8_t link_id);
static uint8_t SecuritySystem_CountLoggedInClients(void);
static void SecuritySystem_SetConfigMirror(void);
static int32_t SecuritySystem_JsonGetInt(const char *json, const char *key, int32_t default_value, uint8_t *found);
static uint8_t SecuritySystem_JsonGetString(const char *json, const char *key, char *out, uint16_t out_size);
static uint8_t SecuritySystem_ApplyDetectionParams(const char *json);
static uint8_t SecuritySystem_ClampAndSetInt(int32_t value, int32_t min_value, int32_t max_value, int32_t *target);
static void SecuritySystem_BuildMenus(void);
static void SecuritySystem_OnArmedToggle(void);
static void SecuritySystem_OnAlarmHoldChanged(int32_t new_value);
static void SecuritySystem_OnFusionWindowChanged(int32_t new_value);
static void SecuritySystem_OnAudioMediumRatioChanged(int32_t new_value);
static void SecuritySystem_OnAudioStrongRatioChanged(int32_t new_value);
static void SecuritySystem_ActionShowStatus(void);
static void SecuritySystem_ActionShowWifi(void);
static void SecuritySystem_ActionSilence(void);
static void SecuritySystem_ActionClear(void);
static void SecuritySystem_RenderStatusPage(void);
static void SecuritySystem_RenderWifiPage(void);
static void SecuritySystem_HandleUi(void);
static void SecuritySystem_ADC_Init(void);
static void SecuritySystem_TIM3_Init(void);
static void SecuritySystem_StartSampling(void);
static void SecuritySystem_RestartTcpServer(void);

static void SecuritySystem_LED_Init(void)
{
    GPIO_InitTypeDef gpio_init;

    SECURITY_LED_CLK_ENABLE();

    memset(&gpio_init, 0, sizeof(gpio_init));
    gpio_init.Pin = SECURITY_LED_PIN;
    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(SECURITY_LED_PORT, &gpio_init);
    SecuritySystem_LED_Write(0u);
}

static void SecuritySystem_LED_Write(uint8_t on)
{
    HAL_GPIO_WritePin(SECURITY_LED_PORT, SECURITY_LED_PIN, (on != 0u) ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

static void SecuritySystem_SetState(SecuritySystem_State_t state)
{
    if (s_status.state != state)
    {
        s_status.state = state;
        s_force_render = 1u;
    }
}

static uint8_t SecuritySystem_HasRecentAudioEvent(uint32_t now_tick)
{
    return (s_last_audio_medium_tick != 0u) &&
           ((now_tick - s_last_audio_medium_tick) <= (uint32_t)s_status.fusion_window_ms);
}

static uint8_t SecuritySystem_HasRecentVibrationEvent(uint32_t now_tick)
{
    return (s_last_vibration_medium_tick != 0u) &&
           ((now_tick - s_last_vibration_medium_tick) <= (uint32_t)s_status.fusion_window_ms);
}

static void SecuritySystem_ResetFusionHistory(void)
{
    s_last_audio_medium_tick = 0u;
    s_last_vibration_medium_tick = 0u;
    SecuritySystem_ResetVotes();
}

static void SecuritySystem_SetLastReason(const char *reason)
{
    if (reason == NULL)
    {
        reason = "NONE";
    }

    strncpy(s_status.last_alarm_reason, reason, sizeof(s_status.last_alarm_reason) - 1u);
    s_status.last_alarm_reason[sizeof(s_status.last_alarm_reason) - 1u] = '\0';
}

static void SecuritySystem_SetArmed(uint8_t armed)
{
    s_status.armed = (armed != 0u) ? 1u : 0u;
    s_armed_toggle = s_status.armed;
    s_status.silenced = 0u;
    SecuritySystem_ResetFusionHistory();

    if (s_status.armed != 0u)
    {
        s_baseline_ready = 0u;
        s_status.audio_baseline_energy = 0u;
        s_status.vibration_baseline_peak_mv = 0u;
        s_status.vibration_baseline_energy = 0u;
        s_warmup_start_tick = HAL_GetTick();
        SecuritySystem_SetState(SECURITY_STATE_WARMUP);
    }
    else
    {
        SecuritySystem_SetState(SECURITY_STATE_DISARMED);
    }
}

static void SecuritySystem_ClearAlarm(void)
{
    uint32_t now_tick = HAL_GetTick();

    s_status.last_alarm_tick = 0u;
    s_status.silenced = 0u;
    SecuritySystem_SetLastReason("NONE");
    SecuritySystem_ResetFusionHistory();

    if (s_status.armed != 0u)
    {
        s_baseline_ready = 0u;
        s_status.audio_baseline_energy = 0u;
        s_status.vibration_baseline_peak_mv = 0u;
        s_status.vibration_baseline_energy = 0u;
        s_warmup_start_tick = now_tick;
        SecuritySystem_SetState(SECURITY_STATE_WARMUP);
    }
    else
    {
        SecuritySystem_SetState(SECURITY_STATE_DISARMED);
    }

    s_last_audio_report_tick = now_tick;
    s_last_vibration_report_tick = now_tick;
    s_last_alarm_report_tick = now_tick;
    s_audio_report_pending = 0u;
    s_vibration_report_pending = 0u;
    s_alarm_report_pending = 0u;
    s_last_audio_report_score = 0u;
    s_last_vibration_report_score = 0u;
    s_last_audio_report_detect_score = 0u;
    s_last_vibration_report_detect_score = 0u;
    s_last_audio_report_level = SIGNAL_LEVEL_NONE;
    s_last_vibration_report_level = SIGNAL_LEVEL_NONE;
}

static void SecuritySystem_TriggerAlarm(const char *reason)
{
    s_status.last_alarm_tick = HAL_GetTick();
    s_status.silenced = 0u;
    SecuritySystem_SetLastReason(reason);
    SecuritySystem_SetState(SECURITY_STATE_ALARM_LATCHED);
    SecuritySystem_RecordAlarm();
    SecuritySystem_BroadcastAlarmReport();
    SecuritySystem_BroadcastStatus();
}

static void SecuritySystem_UpdateBaselines(void)
{
    if (s_baseline_ready == 0u)
    {
        /* First baseline after arm/warmup. */
        s_status.audio_baseline_energy = s_status.audio_features.total_energy;
        s_status.vibration_baseline_peak_mv = s_status.vibration_features.peak_mv;
        s_status.vibration_baseline_energy = s_status.vibration_features.energy;
        s_baseline_ready = 1u;
        return;
    }

    /* EMA(alpha=1/32): tracks slow environment drift, suppresses short impulses. */
    s_status.audio_baseline_energy =
        (s_status.audio_baseline_energy * 31u + s_status.audio_features.total_energy) / 32u;
    s_status.vibration_baseline_peak_mv =
        (s_status.vibration_baseline_peak_mv * 31u + s_status.vibration_features.peak_mv) / 32u;
    s_status.vibration_baseline_energy =
        (s_status.vibration_baseline_energy * 31u + s_status.vibration_features.energy) / 32u;
}

static uint8_t SecuritySystem_RatioScore(uint32_t value, uint32_t medium, uint32_t strong)
{
    uint32_t span;

    if (strong <= medium)
    {
        return (value >= strong) ? 100u : 0u;
    }

    if (value >= strong)
    {
        return 100u;
    }

    if (value <= medium)
    {
        return 0u;
    }

    span = strong - medium;
    return (uint8_t)(((value - medium) * 100u) / span);
}

static uint8_t SecuritySystem_ClampScoreU16(uint16_t value, uint16_t medium, uint16_t strong)
{
    return SecuritySystem_RatioScore((uint32_t)value, (uint32_t)medium, (uint32_t)strong);
}

static uint8_t SecuritySystem_FrequencyScore(uint16_t freq_hz)
{
    uint32_t score;

    if (freq_hz >= SECURITY_FREQUENCY_SCORE_MAX_HZ)
    {
        return 100u;
    }

    score = (((uint32_t)freq_hz * 100u) + (SECURITY_FREQUENCY_SCORE_MAX_HZ / 2u)) /
            SECURITY_FREQUENCY_SCORE_MAX_HZ;
    if (score > 100u)
    {
        score = 100u;
    }

    return (uint8_t)score;
}

static uint8_t SecuritySystem_CountVoteBits(uint8_t value)
{
    uint8_t count = 0u;

    value &= SECURITY_VOTE_WINDOW_BITS;
    while (value != 0u)
    {
        count = (uint8_t)(count + (value & 0x01u));
        value >>= 1;
    }

    return count;
}

static SignalLevel_t SecuritySystem_UpdateVote(SecurityVoteState_t *vote, uint8_t score, uint8_t medium_threshold, uint8_t strong_threshold)
{
    uint8_t medium_hit;
    uint8_t strong_hit;

    if (vote == NULL)
    {
        return SIGNAL_LEVEL_NONE;
    }

    medium_hit = (uint8_t)(score >= medium_threshold);
    strong_hit = (uint8_t)(score >= strong_threshold);
    vote->score = score;
    vote->medium_history = (uint8_t)(((vote->medium_history << 1) | medium_hit) & SECURITY_VOTE_WINDOW_BITS);
    vote->strong_history = (uint8_t)(((vote->strong_history << 1) | strong_hit) & SECURITY_VOTE_WINDOW_BITS);

    if (SecuritySystem_CountVoteBits(vote->strong_history) >= SECURITY_VOTE_STRONG_COUNT)
    {
        return SIGNAL_LEVEL_STRONG;
    }

    if (SecuritySystem_CountVoteBits(vote->medium_history) >= SECURITY_VOTE_MEDIUM_COUNT)
    {
        return SIGNAL_LEVEL_MEDIUM;
    }

    return SIGNAL_LEVEL_NONE;
}

static void SecuritySystem_ResetVotes(void)
{
    memset(&s_audio_vote, 0, sizeof(s_audio_vote));
    memset(&s_vibration_vote, 0, sizeof(s_vibration_vote));
    s_status.audio_score = 0u;
    s_status.vibration_score = 0u;
    s_status.audio_detect_score = 0u;
    s_status.vibration_detect_score = 0u;
}

static SignalLevel_t SecuritySystem_ClassifyAudio(void)
{
    uint32_t medium_threshold = s_status.audio_baseline_energy * 5u / 2u;
    uint32_t strong_threshold = s_status.audio_baseline_energy * 5u;
    uint8_t energy_score;
    uint8_t band_score;
    uint8_t high_score;
    uint8_t rms_score;
    uint16_t high_medium_ratio = (uint16_t)(s_status.audio_medium_ratio_pct / 2);
    uint16_t high_strong_ratio = (uint16_t)((s_status.audio_strong_ratio_pct * 3) / 4);
    uint32_t rms_medium_threshold = 35u;
    uint32_t rms_strong_threshold = 120u;
    uint32_t detect_score;

    if (medium_threshold < SECURITY_AUDIO_MEDIUM_ENERGY_FLOOR)
    {
        medium_threshold = SECURITY_AUDIO_MEDIUM_ENERGY_FLOOR;
    }
    if (strong_threshold < SECURITY_AUDIO_STRONG_ENERGY_FLOOR)
    {
        strong_threshold = SECURITY_AUDIO_STRONG_ENERGY_FLOOR;
    }
    if (strong_threshold <= medium_threshold)
    {
        strong_threshold = medium_threshold + SECURITY_AUDIO_MEDIUM_ENERGY_FLOOR;
    }

    if (high_medium_ratio < 12u)
    {
        high_medium_ratio = 12u;
    }
    if (high_strong_ratio <= high_medium_ratio)
    {
        high_strong_ratio = (uint16_t)(high_medium_ratio + 10u);
    }

    energy_score = SecuritySystem_RatioScore(s_status.audio_features.total_energy, medium_threshold, strong_threshold);
    band_score = SecuritySystem_ClampScoreU16(s_status.audio_features.band_ratio_pct,
                                              (uint16_t)s_status.audio_medium_ratio_pct,
                                              (uint16_t)s_status.audio_strong_ratio_pct);
    high_score = SecuritySystem_ClampScoreU16(s_status.audio_features.high_ratio_pct,
                                              high_medium_ratio,
                                              high_strong_ratio);
    rms_score = SecuritySystem_RatioScore(s_status.audio_features.rms, rms_medium_threshold, rms_strong_threshold);

    detect_score = ((uint32_t)energy_score * 45u) +
                   ((uint32_t)band_score * 25u) +
                   ((uint32_t)high_score * 20u) +
                   ((uint32_t)rms_score * 10u);
    detect_score /= 100u;
    if (detect_score > 100u)
    {
        detect_score = 100u;
    }

    s_status.audio_detect_score = (uint8_t)detect_score;
    s_status.audio_score = SecuritySystem_FrequencyScore(s_status.audio_features.dominant_freq_hz);
    return SecuritySystem_UpdateVote(&s_audio_vote, s_status.audio_detect_score, SECURITY_AUDIO_MEDIUM_SCORE, SECURITY_AUDIO_STRONG_SCORE);
}

static SignalLevel_t SecuritySystem_ClassifyVibration(void)
{
    uint32_t peak_medium_threshold = s_status.vibration_baseline_peak_mv * 18u / 10u;
    uint32_t peak_strong_threshold = s_status.vibration_baseline_peak_mv * 3u;
    uint32_t energy_medium_threshold = s_status.vibration_baseline_energy * 2u;
    uint32_t energy_strong_threshold = s_status.vibration_baseline_energy * 35u / 10u;
    uint8_t freq_valid = (s_status.vibration_features.dominant_freq_hz >= SECURITY_VIBRATION_FREQ_MIN_HZ) &&
                         (s_status.vibration_features.dominant_freq_hz <= SECURITY_VIBRATION_FREQ_MAX_HZ);
    uint8_t peak_score;
    uint8_t energy_score;
    uint8_t crossing_score;
    uint8_t freq_score;
    uint32_t score;

    if (peak_medium_threshold < 150u)
    {
        peak_medium_threshold = 150u;
    }
    if (peak_strong_threshold < 320u)
    {
        peak_strong_threshold = 320u;
    }
    if (energy_medium_threshold < SECURITY_VIBRATION_ENERGY_FLOOR)
    {
        energy_medium_threshold = SECURITY_VIBRATION_ENERGY_FLOOR;
    }
    if (energy_strong_threshold < SECURITY_VIBRATION_STRONG_ENERGY_FLOOR)
    {
        energy_strong_threshold = SECURITY_VIBRATION_STRONG_ENERGY_FLOOR;
    }

    peak_score = SecuritySystem_RatioScore(s_status.vibration_features.peak_mv, peak_medium_threshold, peak_strong_threshold);
    energy_score = SecuritySystem_RatioScore(s_status.vibration_features.energy, energy_medium_threshold, energy_strong_threshold);
    crossing_score = SecuritySystem_ClampScoreU16(s_status.vibration_features.zero_cross_permille, 70u, 180u);
    freq_score = (freq_valid != 0u) ? 100u : 0u;

    score = ((uint32_t)peak_score * 35u) +
            ((uint32_t)energy_score * 35u) +
            ((uint32_t)crossing_score * 20u) +
            ((uint32_t)freq_score * 10u);
    score /= 100u;

    if (freq_valid == 0u)
    {
        score /= 2u;
    }
    if (score > 100u)
    {
        score = 100u;
    }

    s_status.vibration_detect_score = (uint8_t)score;
    s_status.vibration_score = SecuritySystem_FrequencyScore(s_status.vibration_features.dominant_freq_hz);
    return SecuritySystem_UpdateVote(&s_vibration_vote, s_status.vibration_detect_score, SECURITY_VIBRATION_MEDIUM_SCORE, SECURITY_VIBRATION_STRONG_SCORE);
}

static void SecuritySystem_ProcessDetection(SignalLevel_t audio_level, SignalLevel_t vibration_level)
{
    uint32_t now_tick = HAL_GetTick();

    if (s_status.armed == 0u)
    {
        return;
    }

    if (s_status.state == SECURITY_STATE_WARMUP)
    {
        return;
    }

    if (audio_level >= SIGNAL_LEVEL_MEDIUM)
    {
        s_last_audio_medium_tick = now_tick;
    }

    if (vibration_level >= SIGNAL_LEVEL_MEDIUM)
    {
        s_last_vibration_medium_tick = now_tick;
    }

    if (s_status.state == SECURITY_STATE_ALARM_LATCHED)
    {
        return;
    }

    if (vibration_level == SIGNAL_LEVEL_STRONG)
    {
        SecuritySystem_TriggerAlarm("VIB_STRONG");
        return;
    }

    if ((audio_level == SIGNAL_LEVEL_STRONG) && (vibration_level >= SIGNAL_LEVEL_MEDIUM))
    {
        SecuritySystem_TriggerAlarm("AUDIO_STRONG_VIB");
        return;
    }

    if ((SecuritySystem_HasRecentAudioEvent(now_tick) != 0u) &&
        (SecuritySystem_HasRecentVibrationEvent(now_tick) != 0u))
    {
        SecuritySystem_TriggerAlarm("AUDIO_VIB");
        return;
    }

    if ((audio_level >= SIGNAL_LEVEL_MEDIUM) || (vibration_level >= SIGNAL_LEVEL_MEDIUM))
    {
        if (s_status.state != SECURITY_STATE_ALARM_LATCHED)
        {
            SecuritySystem_SetState(SECURITY_STATE_SUSPICIOUS);
        }
    }
}

static void SecuritySystem_ProcessFrame(const volatile uint16_t *frame_data)
{
    SignalLevel_t audio_level = SIGNAL_LEVEL_NONE;
    SignalLevel_t vibration_level = SIGNAL_LEVEL_NONE;
    uint16_t i = 0u;
    uint32_t now_tick;

    if (frame_data == NULL)
    {
        return;
    }

    for (i = 0u; i < MAX4466_FRAME_SAMPLES; i++)
    {
        /* Interleaved DMA frame: [audio, vibration] pairs from ADC rank 1/2. */
        s_audio_frame[i] = (uint16_t)frame_data[i * 2u];
        s_vibration_frame[i] = (uint16_t)frame_data[i * 2u + 1u];
    }

    /* Extract per-frame features for both modalities. */
    MAX4466_AnalyzeFrame(s_audio_frame, MAX4466_FRAME_SAMPLES, &s_status.audio_features);
    PZT_Sensor_AnalyzeFrame(s_vibration_frame, PZT_FRAME_SAMPLES, &s_status.vibration_features);
    now_tick = HAL_GetTick();

    if ((s_status.state == SECURITY_STATE_WARMUP) || (s_status.state == SECURITY_STATE_ARMED))
    {
        audio_level = SecuritySystem_ClassifyAudio();
        vibration_level = SecuritySystem_ClassifyVibration();
        SecuritySystem_BroadcastAudioReport(now_tick, audio_level);
        SecuritySystem_BroadcastVibrationReport(now_tick, vibration_level);

        if ((s_status.state == SECURITY_STATE_WARMUP) ||
            ((audio_level == SIGNAL_LEVEL_NONE) && (vibration_level == SIGNAL_LEVEL_NONE)))
        {
            /* In ARMED state, only quiet frames are used to refresh baseline. */
            SecuritySystem_UpdateBaselines();
        }

        SecuritySystem_ProcessDetection(audio_level, vibration_level);
    }
    else if (s_status.state == SECURITY_STATE_ALARM_LATCHED)
    {
        audio_level = SecuritySystem_ClassifyAudio();
        vibration_level = SecuritySystem_ClassifyVibration();
        SecuritySystem_BroadcastAudioReport(now_tick, audio_level);
        SecuritySystem_BroadcastVibrationReport(now_tick, vibration_level);
        SecuritySystem_ProcessDetection(audio_level, vibration_level);
    }

    if ((s_status.state == SECURITY_STATE_ALARM_LATCHED) ||
        (audio_level != SIGNAL_LEVEL_NONE) ||
        (vibration_level != SIGNAL_LEVEL_NONE))
    {
        s_force_render = 1u;
    }
}

static void SecuritySystem_ProcessPendingFrames(void)
{
    uint8_t frames_processed = 0u;

    while (frames_processed < SECURITY_MAX_FRAMES_PER_TASK)
    {
        uint8_t frame_flag = 0u;

        /* Consume one DMA half/full frame atomically to avoid race with IRQ updates. */
        __disable_irq();
        if ((s_adc_ready_flags & 0x01u) != 0u)
        {
            s_adc_ready_flags &= (uint8_t)(~0x01u);
            frame_flag = 0x01u;
        }
        else if ((s_adc_ready_flags & 0x02u) != 0u)
        {
            s_adc_ready_flags &= (uint8_t)(~0x02u);
            frame_flag = 0x02u;
        }
        __enable_irq();

        if (frame_flag == 0u)
        {
            break;
        }

        if (frame_flag == 0x01u)
        {
            SecuritySystem_ProcessFrame(&s_adc_dma_buffer[0]);
        }
        else
        {
            SecuritySystem_ProcessFrame(&s_adc_dma_buffer[SECURITY_DMA_FRAME_WORDS]);
        }

        frames_processed++;
    }
}

static void SecuritySystem_UpdateOutputs(void)
{
    uint32_t now_tick = HAL_GetTick();
    uint8_t led_on = 0u;
    uint8_t buzzer_on = 0u;

    if (s_status.state == SECURITY_STATE_SUSPICIOUS)
    {
        led_on = (uint8_t)(((now_tick / SECURITY_SUSPICIOUS_BLINK_MS) & 0x1u) == 0u);
    }
    else if (s_status.state == SECURITY_STATE_ALARM_LATCHED)
    {
        led_on = (uint8_t)(((now_tick / SECURITY_ALARM_BLINK_MS) & 0x1u) == 0u);
        if (s_status.silenced == 0u)
        {
            buzzer_on = led_on;
        }
    }

    SecuritySystem_LED_Write(led_on);
    if (buzzer_on != 0u)
    {
        Buzzer_On();
    }
    else
    {
        Buzzer_Off();
    }
}

static void SecuritySystem_MaintainState(void)
{
    uint32_t now_tick = HAL_GetTick();
    SecuritySystem_State_t previous_state = s_status.state;

    if (s_status.armed == 0u)
    {
        SecuritySystem_SetState(SECURITY_STATE_DISARMED);
    }
    else
    {
        if ((s_status.state == SECURITY_STATE_WARMUP) &&
            ((now_tick - s_warmup_start_tick) >= SECURITY_WARMUP_MS))
        {
            SecuritySystem_ResetFusionHistory();
            SecuritySystem_SetState(SECURITY_STATE_ARMED);
        }

        if ((s_status.state == SECURITY_STATE_SUSPICIOUS) &&
            (SecuritySystem_HasRecentAudioEvent(now_tick) == 0u) &&
            (SecuritySystem_HasRecentVibrationEvent(now_tick) == 0u))
        {
            /* Recover to ARMED when no recent medium-level events remain. */
            SecuritySystem_SetState(SECURITY_STATE_ARMED);
        }

        if ((s_status.state == SECURITY_STATE_ALARM_LATCHED) &&
            ((now_tick - s_status.last_alarm_tick) >= (uint32_t)s_status.alarm_hold_ms))
        {
            /* Hold timeout: keep SUSPICIOUS only if activity window is still active. */
            if ((SecuritySystem_HasRecentAudioEvent(now_tick) != 0u) ||
                (SecuritySystem_HasRecentVibrationEvent(now_tick) != 0u))
            {
                SecuritySystem_SetState(SECURITY_STATE_SUSPICIOUS);
            }
            else
            {
                SecuritySystem_SetState(SECURITY_STATE_ARMED);
            }
        }
    }

    if (previous_state != s_status.state)
    {
        SecuritySystem_BroadcastStatus();
    }
}

static void SecuritySystem_HandleEspPacket(ESP_DataPacket_t *packet)
{
    if ((packet == NULL) || (packet->len == 0u))
    {
        return;
    }

    if (packet->link_id < TCP_PROTOCOL_MAX_LINKS)
    {
        s_tcp_clients[packet->link_id].last_seen_tick = HAL_GetTick();
        TcpProtocol_ProcessBytes(packet->link_id, packet->data, packet->len);
    }
}

static void SecuritySystem_HandleEspLinkEvent(uint8_t link_id, ESP_LinkEvent_t event)
{
    if (link_id >= TCP_PROTOCOL_MAX_LINKS)
    {
        return;
    }

    if (event == ESP_LINK_EVENT_CONNECT)
    {
        SecuritySystem_ResetTcpClient(link_id);
        s_tcp_clients[link_id].last_seen_tick = HAL_GetTick();
        return;
    }

    SecuritySystem_ResetTcpClient(link_id);
    if (s_status.active_clients != 0u)
    {
        s_status.active_clients = SecuritySystem_CountLoggedInClients();
    }
}

static uint8_t SecuritySystem_CanSendTcp(uint8_t link_id)
{
    uint32_t now_tick;

    if (link_id >= TCP_PROTOCOL_MAX_LINKS)
    {
        return 0u;
    }

    now_tick = HAL_GetTick();
    if ((s_tcp_clients[link_id].tx_blocked_until_tick != 0u) &&
        ((int32_t)(now_tick - s_tcp_clients[link_id].tx_blocked_until_tick) < 0))
    {
        return 0u;
    }

    s_tcp_clients[link_id].tx_blocked_until_tick = 0u;
    return 1u;
}

static void SecuritySystem_RecordTcpSendResult(uint8_t link_id, uint8_t ok)
{
    if (link_id >= TCP_PROTOCOL_MAX_LINKS)
    {
        return;
    }

    if (ok != 0u)
    {
        s_tcp_clients[link_id].tx_fail_count = 0u;
        s_tcp_clients[link_id].tx_blocked_until_tick = 0u;
        s_tcp_clients[link_id].last_tx_tick = HAL_GetTick();
        if (s_tcp_clients[link_id].tx_ok_count < 0xFFFFFFFFu)
        {
            s_tcp_clients[link_id].tx_ok_count++;
        }
        s_tcp_clients[link_id].last_tx_status = ESP_OK;
        return;
    }

    s_tcp_clients[link_id].last_tx_tick = HAL_GetTick();
    if (s_tcp_clients[link_id].tx_fail_count < 0xFFu)
    {
        s_tcp_clients[link_id].tx_fail_count++;
    }

    if (s_tcp_clients[link_id].tx_fail_count >= SECURITY_TCP_TX_FAIL_LIMIT)
    {
        SecuritySystem_RestartTcpServer();
        return;
    }

    if (s_tcp_clients[link_id].tx_fail_count >= 2u)
    {
        s_tcp_clients[link_id].tx_blocked_until_tick = HAL_GetTick() + SECURITY_TCP_TX_BACKOFF_MS;
    }
}

static uint8_t SecuritySystem_QueuedTcpSendReady(void)
{
    uint32_t now_tick;
    uint32_t last_tx_tick;

    if (s_tcp_active_link >= TCP_PROTOCOL_MAX_LINKS)
    {
        return 1u;
    }

    last_tx_tick = s_tcp_clients[s_tcp_active_link].last_tx_tick;
    if (last_tx_tick == 0u)
    {
        return 1u;
    }

    now_tick = HAL_GetTick();
    return (uint8_t)((now_tick - last_tx_tick) >= SECURITY_TCP_TX_GAP_MS);
}

static uint8_t SecuritySystem_SendTcpFrame(uint8_t link_id, TcpProtocol_MessageType_t type, uint32_t seq, const char *json)
{
    uint8_t header[TCP_PROTOCOL_HEADER_SIZE];
    uint32_t body_len = 0u;
    ESP_Status_t send_status;

    if (link_id >= TCP_PROTOCOL_MAX_LINKS)
    {
        return 0u;
    }

    if (json != NULL)
    {
        body_len = (uint32_t)strlen(json);
    }

    if (body_len > SECURITY_TCP_TX_BODY_SIZE)
    {
        return 0u;
    }

    if ((uint32_t)TCP_PROTOCOL_HEADER_SIZE + body_len > ESP_TX_BUF_SIZE)
    {
        return 0u;
    }

    if (TcpProtocol_BuildHeader(type, seq, body_len, header, sizeof(header)) == 0u)
    {
        return 0u;
    }

    if (SecuritySystem_CanSendTcp(link_id) == 0u)
    {
        return 0u;
    }

    s_tcp_clients[link_id].last_tx_type = (uint16_t)type;
    send_status = ESP_SendDataParts(link_id, header, TCP_PROTOCOL_HEADER_SIZE, (const uint8_t *)json, (uint16_t)body_len);
    if (send_status != ESP_OK)
    {
        if (send_status == ESP_ERR_BUSY)
        {
            s_tcp_clients[link_id].tx_blocked_until_tick = HAL_GetTick() + SECURITY_TCP_TX_BACKOFF_MS;
        }
        else if (send_status == ESP_ERR_TIMEOUT)
        {
            s_tcp_clients[link_id].tx_blocked_until_tick = HAL_GetTick() + SECURITY_TCP_TX_TIMEOUT_BACKOFF_MS;
        }

        s_tcp_clients[link_id].last_tx_status = (uint8_t)send_status;
        SecuritySystem_RecordTcpSendResult(link_id, 0u);
        return 0u;
    }

    SecuritySystem_RecordTcpSendResult(link_id, 1u);
    return 1u;
}

static uint8_t SecuritySystem_SendBytesChunked(uint8_t link_id, const uint8_t *data, uint32_t len)
{
    uint32_t offset = 0u;
    ESP_Status_t send_status;

    if (link_id >= TCP_PROTOCOL_MAX_LINKS)
    {
        return 0u;
    }

    if ((data == NULL) || (len == 0u))
    {
        return 1u;
    }

    if (SecuritySystem_IsLinkActive(link_id) == 0u)
    {
        SecuritySystem_ResetTcpClient(link_id);
        return 0u;
    }

    while (offset < len)
    {
        uint32_t remain = len - offset;
        uint16_t chunk_len = (remain > ESP_TX_BUF_SIZE) ? ESP_TX_BUF_SIZE : (uint16_t)remain;

        if (SecuritySystem_CanSendTcp(link_id) == 0u)
        {
            return 0u;
        }

        send_status = ESP_SendData(link_id, &data[offset], chunk_len);
        if (send_status != ESP_OK)
        {
            if (send_status == ESP_ERR_BUSY)
            {
                s_tcp_clients[link_id].tx_blocked_until_tick = HAL_GetTick() + SECURITY_TCP_TX_BACKOFF_MS;
            }
            else if (send_status == ESP_ERR_TIMEOUT)
            {
                s_tcp_clients[link_id].tx_blocked_until_tick = HAL_GetTick() + SECURITY_TCP_TX_TIMEOUT_BACKOFF_MS;
            }

            s_tcp_clients[link_id].last_tx_status = (uint8_t)send_status;
            SecuritySystem_RecordTcpSendResult(link_id, 0u);
            return 0u;
        }

        SecuritySystem_RecordTcpSendResult(link_id, 1u);
        offset += chunk_len;
    }

    return 1u;
}

static void SecuritySystem_SendError(uint8_t link_id, uint32_t seq, TcpProtocol_ErrorCode_t error, const char *detail)
{
    int written;

    if (detail == NULL)
    {
        detail = "";
    }

    written = snprintf(
        s_tcp_body,
        sizeof(s_tcp_body),
        "{\"ok\":false,\"code\":\"%s\",\"detail\":\"%s\"}",
        TcpProtocol_ErrorText(error),
        detail);

    if ((written > 0) && ((uint32_t)written < sizeof(s_tcp_body)))
    {
        (void)SecuritySystem_SendTcpFrame(link_id, TCP_MSG_ERROR_RSP, seq, s_tcp_body);
    }
}

static uint8_t SecuritySystem_BuildStatusJson(char *buffer, uint16_t size)
{
    int written;

    if ((buffer == NULL) || (size == 0u))
    {
        return 0u;
    }

    written = snprintf(
        buffer,
        size,
        "{\"device_id\":\"%s\",\"tick\":%lu,\"state\":\"%s\",\"state_code\":%u,"
        "\"armed\":%u,\"silenced\":%u,\"reason\":\"%s\",\"last_alarm_tick\":%lu,"
        "\"audio\":{\"freq_hz\":%u,\"energy\":%lu,\"ratio_pct\":%u,\"high_ratio_pct\":%u,\"rms_mv\":%lu,\"score\":%u,\"detect_score\":%u},"
        "\"vibration\":{\"freq_hz\":%u,\"peak_mv\":%lu,\"energy\":%lu,\"zero_cross_permille\":%u,\"score\":%u,\"detect_score\":%u},"
        "\"config\":{\"alarm_hold_ms\":%ld,\"fusion_window_ms\":%ld,"
        "\"audio_medium_ratio_pct\":%ld,\"audio_strong_ratio_pct\":%ld},"
        "\"wifi_ready\":%u,\"clients\":%u,\"tcp\":{\"link_mask\":%u,\"active_link\":%u,"
        "\"logged_in\":%u,\"tx_fail_count\":%u,\"last_tx_status\":%u,\"blocked_ms\":%lu,"
        "\"last_rx_type\":%u,\"last_tx_type\":%u,\"tx_ok_count\":%lu,\"last_tx_ms_ago\":%lu,"
        "\"uart_err\":%lu,\"rx_ovf\":%lu}}",
        SECURITY_TCP_DEVICE_ID,
        (unsigned long)HAL_GetTick(),
        SecuritySystem_StateText(s_status.state),
        (unsigned int)s_status.state,
        (unsigned int)s_status.armed,
        (unsigned int)s_status.silenced,
        s_status.last_alarm_reason,
        (unsigned long)s_status.last_alarm_tick,
        (unsigned int)s_status.audio_features.dominant_freq_hz,
        (unsigned long)s_status.audio_features.total_energy,
        (unsigned int)s_status.audio_features.band_ratio_pct,
        (unsigned int)s_status.audio_features.high_ratio_pct,
        (unsigned long)s_status.audio_features.rms,
        (unsigned int)s_status.audio_score,
        (unsigned int)s_status.audio_detect_score,
        (unsigned int)s_status.vibration_features.dominant_freq_hz,
        (unsigned long)s_status.vibration_features.peak_mv,
        (unsigned long)s_status.vibration_features.energy,
        (unsigned int)s_status.vibration_features.zero_cross_permille,
        (unsigned int)s_status.vibration_score,
        (unsigned int)s_status.vibration_detect_score,
        (long)s_status.alarm_hold_ms,
        (long)s_status.fusion_window_ms,
        (long)s_status.audio_medium_ratio_pct,
        (long)s_status.audio_strong_ratio_pct,
        (unsigned int)s_status.wifi_ready,
        (unsigned int)s_status.active_clients,
        (unsigned int)ESP_GetActiveLinkMask(),
        (unsigned int)((s_tcp_active_link < TCP_PROTOCOL_MAX_LINKS) ? s_tcp_active_link : 255u),
        (unsigned int)((s_tcp_active_link < TCP_PROTOCOL_MAX_LINKS) ? s_tcp_clients[s_tcp_active_link].logged_in : 0u),
        (unsigned int)((s_tcp_active_link < TCP_PROTOCOL_MAX_LINKS) ? s_tcp_clients[s_tcp_active_link].tx_fail_count : 0u),
        (unsigned int)((s_tcp_active_link < TCP_PROTOCOL_MAX_LINKS) ? s_tcp_clients[s_tcp_active_link].last_tx_status : 0u),
        (unsigned long)(((s_tcp_active_link < TCP_PROTOCOL_MAX_LINKS) &&
                         (s_tcp_clients[s_tcp_active_link].tx_blocked_until_tick > HAL_GetTick()))
                            ? (s_tcp_clients[s_tcp_active_link].tx_blocked_until_tick - HAL_GetTick())
                            : 0u),
        (unsigned int)((s_tcp_active_link < TCP_PROTOCOL_MAX_LINKS) ? s_tcp_clients[s_tcp_active_link].last_rx_type : 0u),
        (unsigned int)((s_tcp_active_link < TCP_PROTOCOL_MAX_LINKS) ? s_tcp_clients[s_tcp_active_link].last_tx_type : 0u),
        (unsigned long)((s_tcp_active_link < TCP_PROTOCOL_MAX_LINKS) ? s_tcp_clients[s_tcp_active_link].tx_ok_count : 0u),
        (unsigned long)(((s_tcp_active_link < TCP_PROTOCOL_MAX_LINKS) &&
                         (s_tcp_clients[s_tcp_active_link].last_tx_tick != 0u))
                            ? (HAL_GetTick() - s_tcp_clients[s_tcp_active_link].last_tx_tick)
                            : 0u),
        (unsigned long)ESP_GetUartErrorCount(),
        (unsigned long)ESP_GetRxOverflowCount());

    return (uint8_t)((written > 0) && ((uint32_t)written < (uint32_t)size));
}

static void SecuritySystem_SendStatus(uint8_t link_id, TcpProtocol_MessageType_t type, uint32_t seq)
{
    if (SecuritySystem_BuildStatusJson(s_tcp_body, sizeof(s_tcp_body)) != 0u)
    {
        (void)SecuritySystem_SendTcpFrame(link_id, type, seq, s_tcp_body);
    }
}

static void SecuritySystem_BroadcastStatus(void)
{
    s_status_push_pending = 1u;
}

static uint8_t SecuritySystem_SendPendingStatus(void)
{
    uint8_t link_id;
    uint8_t sent = 0u;

    if (s_status_push_pending == 0u)
    {
        return 0u;
    }

    if (SecuritySystem_BuildStatusJson(s_tcp_body, sizeof(s_tcp_body)) == 0u)
    {
        s_status_push_pending = 0u;
        return 0u;
    }

    for (link_id = 0u; link_id < TCP_PROTOCOL_MAX_LINKS; link_id++)
    {
        if (SecuritySystem_IsLoggedIn(link_id) != 0u)
        {
            if (SecuritySystem_SendTcpFrame(link_id, TCP_MSG_STATUS_PUSH, s_tcp_seq++, s_tcp_body) == 0u)
            {
                SecuritySystem_DropRealtimePendingAfterTxFailure(TCP_MSG_STATUS_PUSH);
                continue;
            }
            sent = 1u;
        }
    }

    s_status_push_pending = 0u;
    return sent;
}

static uint32_t SecuritySystem_ReportIntervalForLevel(SignalLevel_t level)
{
    if ((level == SIGNAL_LEVEL_NONE) && (s_status.state != SECURITY_STATE_ALARM_LATCHED))
    {
        return SECURITY_TCP_IDLE_REPORT_INTERVAL_MS;
    }

    if (level == SIGNAL_LEVEL_STRONG)
    {
        return SECURITY_TCP_REPORT_INTERVAL_MS;
    }

    return (SECURITY_TCP_REPORT_INTERVAL_MS * 2u) / 3u;
}

static void SecuritySystem_BroadcastAudioReport(uint32_t tick, SignalLevel_t level)
{
    uint32_t min_interval;

    if ((s_status.active_clients == 0u) || (level == SIGNAL_LEVEL_NONE))
    {
        return;
    }

    min_interval = SecuritySystem_ReportIntervalForLevel(level);
    if (((tick - s_last_audio_report_tick) < min_interval) &&
        (level == s_last_audio_report_level) &&
        ((uint8_t)abs((int)s_status.audio_score - (int)s_last_audio_report_score) < SECURITY_TCP_REPORT_SCORE_DELTA) &&
        ((uint8_t)abs((int)s_status.audio_detect_score - (int)s_last_audio_report_detect_score) < SECURITY_TCP_REPORT_SCORE_DELTA))
    {
        return;
    }
    s_last_audio_report_tick = tick;
    s_last_audio_report_level = level;
    s_last_audio_report_score = s_status.audio_score;
    s_last_audio_report_detect_score = s_status.audio_detect_score;
    s_pending_audio_report_tick = tick;
    s_pending_audio_report_level = level;
    s_audio_report_pending = 1u;
}

static uint8_t SecuritySystem_SendPendingAudioReport(void)
{
    uint8_t link_id;
    uint8_t sent = 0u;
    int written;

    if (s_audio_report_pending == 0u)
    {
        return 0u;
    }

    written = snprintf(
        s_tcp_body,
        sizeof(s_tcp_body),
        "{\"tick\":%lu,\"level\":%u,\"freq_hz\":%u,\"energy\":%lu,\"ratio_pct\":%u,\"high_ratio_pct\":%u,\"rms_mv\":%lu,\"score\":%u,\"detect_score\":%u}",
        (unsigned long)s_pending_audio_report_tick,
        (unsigned int)s_pending_audio_report_level,
        (unsigned int)s_status.audio_features.dominant_freq_hz,
        (unsigned long)s_status.audio_features.total_energy,
        (unsigned int)s_status.audio_features.band_ratio_pct,
        (unsigned int)s_status.audio_features.high_ratio_pct,
        (unsigned long)s_status.audio_features.rms,
        (unsigned int)s_status.audio_score,
        (unsigned int)s_status.audio_detect_score);

    if ((written <= 0) || ((uint32_t)written >= sizeof(s_tcp_body)))
    {
        s_audio_report_pending = 0u;
        return 0u;
    }

    for (link_id = 0u; link_id < TCP_PROTOCOL_MAX_LINKS; link_id++)
    {
        if (SecuritySystem_IsLoggedIn(link_id) != 0u)
        {
            if (SecuritySystem_SendTcpFrame(link_id, TCP_MSG_AUDIO_REPORT, s_tcp_seq++, s_tcp_body) == 0u)
            {
                SecuritySystem_DropRealtimePendingAfterTxFailure(TCP_MSG_AUDIO_REPORT);
                continue;
            }
            sent = 1u;
        }
    }

    s_audio_report_pending = 0u;
    return sent;
}

static void SecuritySystem_BroadcastVibrationReport(uint32_t tick, SignalLevel_t level)
{
    uint32_t min_interval;

    if ((s_status.active_clients == 0u) || (level == SIGNAL_LEVEL_NONE))
    {
        return;
    }

    min_interval = SecuritySystem_ReportIntervalForLevel(level);
    if (((tick - s_last_vibration_report_tick) < min_interval) &&
        (level == s_last_vibration_report_level) &&
        ((uint8_t)abs((int)s_status.vibration_score - (int)s_last_vibration_report_score) < SECURITY_TCP_REPORT_SCORE_DELTA) &&
        ((uint8_t)abs((int)s_status.vibration_detect_score - (int)s_last_vibration_report_detect_score) < SECURITY_TCP_REPORT_SCORE_DELTA))
    {
        return;
    }
    s_last_vibration_report_tick = tick;
    s_last_vibration_report_level = level;
    s_last_vibration_report_score = s_status.vibration_score;
    s_last_vibration_report_detect_score = s_status.vibration_detect_score;
    s_pending_vibration_report_tick = tick;
    s_pending_vibration_report_level = level;
    s_vibration_report_pending = 1u;
}

static uint8_t SecuritySystem_SendPendingVibrationReport(void)
{
    uint8_t link_id;
    uint8_t sent = 0u;
    int written;

    if (s_vibration_report_pending == 0u)
    {
        return 0u;
    }

    written = snprintf(
        s_tcp_body,
        sizeof(s_tcp_body),
        "{\"tick\":%lu,\"level\":%u,\"freq_hz\":%u,\"peak_mv\":%lu,\"energy\":%lu,\"zero_cross_permille\":%u,\"score\":%u,\"detect_score\":%u}",
        (unsigned long)s_pending_vibration_report_tick,
        (unsigned int)s_pending_vibration_report_level,
        (unsigned int)s_status.vibration_features.dominant_freq_hz,
        (unsigned long)s_status.vibration_features.peak_mv,
        (unsigned long)s_status.vibration_features.energy,
        (unsigned int)s_status.vibration_features.zero_cross_permille,
        (unsigned int)s_status.vibration_score,
        (unsigned int)s_status.vibration_detect_score);

    if ((written <= 0) || ((uint32_t)written >= sizeof(s_tcp_body)))
    {
        s_vibration_report_pending = 0u;
        return 0u;
    }

    for (link_id = 0u; link_id < TCP_PROTOCOL_MAX_LINKS; link_id++)
    {
        if (SecuritySystem_IsLoggedIn(link_id) != 0u)
        {
            if (SecuritySystem_SendTcpFrame(link_id, TCP_MSG_VIBRATION_REPORT, s_tcp_seq++, s_tcp_body) == 0u)
            {
                SecuritySystem_DropRealtimePendingAfterTxFailure(TCP_MSG_VIBRATION_REPORT);
                continue;
            }
            sent = 1u;
        }
    }

    s_vibration_report_pending = 0u;
    return sent;
}

static void SecuritySystem_BroadcastAlarmReport(void)
{
    uint32_t now_tick = HAL_GetTick();

    if ((s_last_alarm_report_tick != 0u) &&
        ((now_tick - s_last_alarm_report_tick) < SECURITY_ALARM_REPORT_MIN_INTERVAL_MS))
    {
        return;
    }
    s_last_alarm_report_tick = now_tick;
    s_alarm_report_pending = 1u;
}

static uint8_t SecuritySystem_SendPendingAlarmReport(void)
{
    uint8_t link_id;
    uint8_t sent = 0u;
    int written;

    if (s_alarm_report_pending == 0u)
    {
        return 0u;
    }

    written = snprintf(
        s_tcp_body,
        sizeof(s_tcp_body),
        "{\"tick\":%lu,\"reason\":\"%s\",\"state\":\"%s\",\"state_code\":%u,"
        "\"audio\":{\"freq_hz\":%u,\"energy\":%lu,\"ratio_pct\":%u,\"score\":%u,\"detect_score\":%u},"
        "\"vibration\":{\"freq_hz\":%u,\"peak_mv\":%lu,\"energy\":%lu,\"score\":%u,\"detect_score\":%u}}",
        (unsigned long)s_status.last_alarm_tick,
        s_status.last_alarm_reason,
        SecuritySystem_StateText(s_status.state),
        (unsigned int)s_status.state,
        (unsigned int)s_status.audio_features.dominant_freq_hz,
        (unsigned long)s_status.audio_features.total_energy,
        (unsigned int)s_status.audio_features.band_ratio_pct,
        (unsigned int)s_status.audio_score,
        (unsigned int)s_status.audio_detect_score,
        (unsigned int)s_status.vibration_features.dominant_freq_hz,
        (unsigned long)s_status.vibration_features.peak_mv,
        (unsigned long)s_status.vibration_features.energy,
        (unsigned int)s_status.vibration_score,
        (unsigned int)s_status.vibration_detect_score);

    if ((written <= 0) || ((uint32_t)written >= sizeof(s_tcp_body)))
    {
        s_alarm_report_pending = 0u;
        return 0u;
    }

    for (link_id = 0u; link_id < TCP_PROTOCOL_MAX_LINKS; link_id++)
    {
        if (SecuritySystem_IsLoggedIn(link_id) != 0u)
        {
            if (SecuritySystem_SendTcpFrame(link_id, TCP_MSG_ALARM_REPORT, s_tcp_seq++, s_tcp_body) == 0u)
            {
                continue;
            }
            sent = 1u;
        }
    }

    s_alarm_report_pending = 0u;
    return sent;
}

static void SecuritySystem_FlushPendingTcpReports(void)
{
    uint8_t send_count = 0u;

    if (SecuritySystem_QueuedTcpSendReady() == 0u)
    {
        return;
    }

    while (send_count < SECURITY_TCP_MAINTAIN_SEND_BUDGET)
    {
        if (s_alarm_report_pending != 0u)
        {
            if (SecuritySystem_SendPendingAlarmReport() == 0u)
            {
                return;
            }
            send_count++;
            continue;
        }

        if (s_status_push_pending != 0u)
        {
            if (SecuritySystem_SendPendingStatus() == 0u)
            {
                return;
            }
            send_count++;
            continue;
        }

        if (s_audio_report_pending != 0u)
        {
            if (SecuritySystem_SendPendingAudioReport() == 0u)
            {
                return;
            }
            send_count++;
            continue;
        }

        if (s_vibration_report_pending != 0u)
        {
            if (SecuritySystem_SendPendingVibrationReport() == 0u)
            {
                return;
            }
            send_count++;
            continue;
        }

        break;
    }
}

static void SecuritySystem_DropRealtimePendingAfterTxFailure(TcpProtocol_MessageType_t type)
{
    if (type == TCP_MSG_STATUS_PUSH)
    {
        s_status_push_pending = 0u;
    }
    else if (type == TCP_MSG_AUDIO_REPORT)
    {
        s_audio_report_pending = 0u;
    }
    else if (type == TCP_MSG_VIBRATION_REPORT)
    {
        s_vibration_report_pending = 0u;
    }
}

static void SecuritySystem_RecordAlarm(void)
{
    SecurityAlarmRecord_t *record = &s_alarm_history[s_alarm_history_head];

    record->tick = s_status.last_alarm_tick;
    record->state = (uint8_t)s_status.state;
    strncpy(record->reason, s_status.last_alarm_reason, sizeof(record->reason) - 1u);
    record->reason[sizeof(record->reason) - 1u] = '\0';
    record->audio_freq_hz = s_status.audio_features.dominant_freq_hz;
    record->audio_energy = s_status.audio_features.total_energy;
    record->audio_ratio_pct = s_status.audio_features.band_ratio_pct;
    record->vibration_freq_hz = s_status.vibration_features.dominant_freq_hz;
    record->vibration_peak_mv = s_status.vibration_features.peak_mv;
    record->vibration_energy = s_status.vibration_features.energy;

    s_alarm_history_head = (uint8_t)((s_alarm_history_head + 1u) % SECURITY_ALARM_HISTORY_SIZE);
    if (s_alarm_history_count < SECURITY_ALARM_HISTORY_SIZE)
    {
        s_alarm_history_count++;
    }
}

static uint16_t SecuritySystem_FormatHistoryItem(const SecurityAlarmRecord_t *record, uint8_t first_item, char *buffer, uint16_t size)
{
    int written;

    if ((record == NULL) || (buffer == NULL) || (size == 0u))
    {
        return 0u;
    }

    written = snprintf(
        buffer,
        size,
        "%s{\"t\":%lu,\"r\":\"%s\",\"s\":%u,"
        "\"af\":%u,\"ae\":%lu,\"ar\":%u,"
        "\"vf\":%u,\"vp\":%lu,\"ve\":%lu}",
        (first_item != 0u) ? "" : ",",
        (unsigned long)record->tick,
        record->reason,
        (unsigned int)record->state,
        (unsigned int)record->audio_freq_hz,
        (unsigned long)record->audio_energy,
        (unsigned int)record->audio_ratio_pct,
        (unsigned int)record->vibration_freq_hz,
        (unsigned long)record->vibration_peak_mv,
        (unsigned long)record->vibration_energy);

    if ((written <= 0) || ((uint32_t)written >= size))
    {
        return 0u;
    }

    return (uint16_t)written;
}

static void SecuritySystem_SendHistory(uint8_t link_id, uint32_t seq)
{
    uint8_t i;
    uint8_t oldest;
    uint32_t body_len;
    uint16_t body_offset;
    uint8_t header[TCP_PROTOCOL_HEADER_SIZE];
    uint8_t fit_single_frame;
    int written;
    char item_buffer[160];

    written = snprintf(s_tcp_body, sizeof(s_tcp_body), "{\"count\":%u,\"items\":[", (unsigned int)s_alarm_history_count);
    if (written <= 0)
    {
        return;
    }
    body_len = (uint32_t)written;
    body_offset = (uint16_t)written;
    fit_single_frame = (uint8_t)((uint32_t)written < sizeof(s_tcp_body));

    oldest = (s_alarm_history_count == SECURITY_ALARM_HISTORY_SIZE) ? s_alarm_history_head : 0u;
    for (i = 0u; i < s_alarm_history_count; i++)
    {
        uint8_t index = (uint8_t)((oldest + i) % SECURITY_ALARM_HISTORY_SIZE);
        SecurityAlarmRecord_t *record = &s_alarm_history[index];
        uint16_t item_len = SecuritySystem_FormatHistoryItem(record, (uint8_t)(i == 0u), item_buffer, sizeof(item_buffer));

        if (item_len == 0u)
        {
            return;
        }

        body_len += item_len;
        if (fit_single_frame != 0u)
        {
            if (((uint32_t)body_offset + item_len) < sizeof(s_tcp_body))
            {
                memcpy(&s_tcp_body[body_offset], item_buffer, item_len);
                body_offset = (uint16_t)(body_offset + item_len);
            }
            else
            {
                fit_single_frame = 0u;
            }
        }
    }
    body_len += 2u;

    if ((fit_single_frame != 0u) && (((uint32_t)body_offset + 2u) < sizeof(s_tcp_body)))
    {
        s_tcp_body[body_offset++] = ']';
        s_tcp_body[body_offset++] = '}';
        s_tcp_body[body_offset] = '\0';
        (void)SecuritySystem_SendTcpFrame(link_id, TCP_MSG_HISTORY_RSP, seq, s_tcp_body);
        return;
    }

    if (TcpProtocol_BuildHeader(TCP_MSG_HISTORY_RSP, seq, body_len, header, sizeof(header)) == 0u)
    {
        return;
    }

    if (SecuritySystem_SendBytesChunked(link_id, header, TCP_PROTOCOL_HEADER_SIZE) == 0u)
    {
        return;
    }

    written = snprintf(s_tcp_body, sizeof(s_tcp_body), "{\"count\":%u,\"items\":[", (unsigned int)s_alarm_history_count);
    if ((written <= 0) || (SecuritySystem_SendBytesChunked(link_id, (const uint8_t *)s_tcp_body, (uint32_t)written) == 0u))
    {
        SecuritySystem_CloseTcpClient(link_id);
        return;
    }

    for (i = 0u; i < s_alarm_history_count; i++)
    {
        uint8_t index = (uint8_t)((oldest + i) % SECURITY_ALARM_HISTORY_SIZE);
        uint16_t item_len = SecuritySystem_FormatHistoryItem(&s_alarm_history[index], (uint8_t)(i == 0u), item_buffer, sizeof(item_buffer));

        if ((item_len == 0u) || (SecuritySystem_SendBytesChunked(link_id, (const uint8_t *)item_buffer, item_len) == 0u))
        {
            SecuritySystem_CloseTcpClient(link_id);
            return;
        }
    }

    if (SecuritySystem_SendBytesChunked(link_id, (const uint8_t *)"]}", 2u) == 0u)
    {
        SecuritySystem_CloseTcpClient(link_id);
    }
}

static void SecuritySystem_MaintainTcpClients(void)
{
    uint8_t link_id;
    uint8_t active_mask = ESP_GetActiveLinkMask();
    uint8_t retained_mask = active_mask;
    uint32_t now_tick = HAL_GetTick();

    for (link_id = 0u; link_id < TCP_PROTOCOL_MAX_LINKS; link_id++)
    {
        uint8_t mask = (uint8_t)(1u << link_id);

        if ((active_mask & mask) == 0u)
        {
            SecuritySystem_ResetTcpClient(link_id);
        }
        else
        {
            if (s_tcp_clients[link_id].last_seen_tick == 0u)
            {
                s_tcp_clients[link_id].last_seen_tick = now_tick;
            }

            if (s_tcp_clients[link_id].logged_in != 0u)
            {
                if ((now_tick - s_tcp_clients[link_id].last_seen_tick) > SECURITY_TCP_CLIENT_TIMEOUT_MS)
                {
                    SecuritySystem_CloseTcpClient(link_id);
                    retained_mask &= (uint8_t)(~mask);
                }
            }
            else if ((now_tick - s_tcp_clients[link_id].last_seen_tick) > SECURITY_TCP_LOGIN_TIMEOUT_MS)
            {
                SecuritySystem_CloseTcpClient(link_id);
                retained_mask &= (uint8_t)(~mask);
            }
        }
    }

    if (retained_mask != active_mask)
    {
        active_mask = retained_mask;
        ESP_RefreshLinkMask(active_mask);
    }

    if (active_mask != s_last_link_mask)
    {
        s_last_link_mask = active_mask;
        s_force_render = 1u;
    }

    if ((s_tcp_active_link < TCP_PROTOCOL_MAX_LINKS) &&
        ((active_mask & (uint8_t)(1u << s_tcp_active_link)) == 0u))
    {
        s_tcp_active_link = SECURITY_TCP_NO_ACTIVE_LINK;
    }

    if ((now_tick - s_last_status_push_tick) >= SECURITY_TCP_PUSH_INTERVAL_MS)
    {
        s_last_status_push_tick = now_tick;
        SecuritySystem_BroadcastStatus();
    }

    SecuritySystem_FlushPendingTcpReports();
}

static void SecuritySystem_CloseTcpClient(uint8_t link_id)
{
    if (link_id >= TCP_PROTOCOL_MAX_LINKS)
    {
        return;
    }

    (void)ESP_CloseLink(link_id);
    SecuritySystem_ResetTcpClient(link_id);
}

static void SecuritySystem_ResetTcpClient(uint8_t link_id)
{
    if (link_id >= TCP_PROTOCOL_MAX_LINKS)
    {
        return;
    }

    s_tcp_clients[link_id].logged_in = 0u;
    s_tcp_clients[link_id].tx_fail_count = 0u;
    s_tcp_clients[link_id].last_seen_tick = 0u;
    s_tcp_clients[link_id].tx_blocked_until_tick = 0u;
    s_tcp_clients[link_id].last_tx_tick = 0u;
    s_tcp_clients[link_id].tx_ok_count = 0u;
    s_tcp_clients[link_id].last_rx_type = 0u;
    s_tcp_clients[link_id].last_tx_type = 0u;
    s_tcp_clients[link_id].last_tx_status = 0u;
    TcpProtocol_ResetLink(link_id);
    if (s_tcp_active_link == link_id)
    {
        s_tcp_active_link = SECURITY_TCP_NO_ACTIVE_LINK;
    }
}

static void SecuritySystem_SelectTcpClient(uint8_t link_id)
{
    uint8_t other;
    uint32_t now_tick;

    if (link_id >= TCP_PROTOCOL_MAX_LINKS)
    {
        return;
    }

    for (other = 0u; other < TCP_PROTOCOL_MAX_LINKS; other++)
    {
        if (other != link_id)
        {
            if ((SecuritySystem_IsLinkActive(other) != 0u) ||
                (s_tcp_clients[other].logged_in != 0u) ||
                (s_tcp_clients[other].last_seen_tick != 0u))
            {
                SecuritySystem_CloseTcpClient(other);
            }
            else
            {
                SecuritySystem_ResetTcpClient(other);
            }
        }
    }

    now_tick = HAL_GetTick();
    s_tcp_clients[link_id].logged_in = 0u;
    s_tcp_clients[link_id].tx_fail_count = 0u;
    s_tcp_clients[link_id].last_seen_tick = now_tick;
    s_tcp_clients[link_id].tx_blocked_until_tick = 0u;
    s_tcp_clients[link_id].last_tx_tick = 0u;
    s_tcp_clients[link_id].tx_ok_count = 0u;
    s_tcp_clients[link_id].last_rx_type = 0u;
    s_tcp_clients[link_id].last_tx_type = 0u;
    s_tcp_clients[link_id].last_tx_status = 0u;
    s_tcp_active_link = link_id;
}

static uint8_t SecuritySystem_IsLinkActive(uint8_t link_id)
{
    if (link_id >= TCP_PROTOCOL_MAX_LINKS)
    {
        return 0u;
    }

    return (uint8_t)((ESP_GetActiveLinkMask() & (uint8_t)(1u << link_id)) != 0u);
}

static uint8_t SecuritySystem_IsLoggedIn(uint8_t link_id)
{
    if (link_id >= TCP_PROTOCOL_MAX_LINKS)
    {
        return 0u;
    }

    return (uint8_t)((s_tcp_clients[link_id].logged_in != 0u) &&
                     (s_tcp_active_link == link_id) &&
                     (SecuritySystem_IsLinkActive(link_id) != 0u));
}

static uint8_t SecuritySystem_CountLoggedInClients(void)
{
    uint8_t link_id;
    uint8_t count = 0u;

    for (link_id = 0u; link_id < TCP_PROTOCOL_MAX_LINKS; link_id++)
    {
        if (SecuritySystem_IsLoggedIn(link_id) != 0u)
        {
            count++;
        }
    }

    return count;
}

static void SecuritySystem_SetConfigMirror(void)
{
    s_alarm_hold_setting = s_status.alarm_hold_ms;
    s_fusion_window_setting = s_status.fusion_window_ms;
    s_audio_medium_ratio_setting = s_status.audio_medium_ratio_pct;
    s_audio_strong_ratio_setting = s_status.audio_strong_ratio_pct;
}

static int32_t SecuritySystem_JsonGetInt(const char *json, const char *key, int32_t default_value, uint8_t *found)
{
    char pattern[48];
    const char *pos;
    char *end_ptr;
    long value;

    if (found != NULL)
    {
        *found = 0u;
    }

    if ((json == NULL) || (key == NULL))
    {
        return default_value;
    }

    (void)snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    pos = strstr(json, pattern);
    if (pos == NULL)
    {
        return default_value;
    }

    pos = strchr(pos, ':');
    if (pos == NULL)
    {
        return default_value;
    }

    pos++;
    while ((*pos == ' ') || (*pos == '\t'))
    {
        pos++;
    }

    value = strtol(pos, &end_ptr, 10);
    if (end_ptr == pos)
    {
        if (strncmp(pos, "true", 4u) == 0)
        {
            value = 1;
            end_ptr = (char *)(pos + 4u);
        }
        else if (strncmp(pos, "false", 5u) == 0)
        {
            value = 0;
            end_ptr = (char *)(pos + 5u);
        }
        else
        {
            return default_value;
        }
    }

    if (found != NULL)
    {
        *found = 1u;
    }
    return (int32_t)value;
}

static uint8_t SecuritySystem_JsonGetString(const char *json, const char *key, char *out, uint16_t out_size)
{
    char pattern[48];
    const char *pos;
    const char *end;
    uint16_t len;

    if ((json == NULL) || (key == NULL) || (out == NULL) || (out_size == 0u))
    {
        return 0u;
    }

    out[0] = '\0';
    (void)snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    pos = strstr(json, pattern);
    if (pos == NULL)
    {
        return 0u;
    }

    pos = strchr(pos, ':');
    if (pos == NULL)
    {
        return 0u;
    }

    pos++;
    while ((*pos == ' ') || (*pos == '\t'))
    {
        pos++;
    }

    if (*pos != '"')
    {
        return 0u;
    }

    pos++;
    end = strchr(pos, '"');
    if (end == NULL)
    {
        return 0u;
    }

    len = (uint16_t)(end - pos);
    if (len >= out_size)
    {
        len = (uint16_t)(out_size - 1u);
    }

    memcpy(out, pos, len);
    out[len] = '\0';
    return 1u;
}

static uint8_t SecuritySystem_ClampAndSetInt(int32_t value, int32_t min_value, int32_t max_value, int32_t *target)
{
    if (target == NULL)
    {
        return 0u;
    }

    if ((value < min_value) || (value > max_value))
    {
        return 0u;
    }

    *target = value;
    return 1u;
}

static uint8_t SecuritySystem_ApplyDetectionParams(const char *json)
{
    uint8_t found;
    int32_t value;

    value = SecuritySystem_JsonGetInt(json, "alarm_hold_ms", s_status.alarm_hold_ms, &found);
    if ((found != 0u) &&
        (SecuritySystem_ClampAndSetInt(value, SECURITY_PARAM_HOLD_MIN, SECURITY_PARAM_HOLD_MAX, &s_status.alarm_hold_ms) == 0u))
    {
        return 0u;
    }

    value = SecuritySystem_JsonGetInt(json, "fusion_window_ms", s_status.fusion_window_ms, &found);
    if ((found != 0u) &&
        (SecuritySystem_ClampAndSetInt(value, SECURITY_PARAM_FUSION_MIN, SECURITY_PARAM_FUSION_MAX, &s_status.fusion_window_ms) == 0u))
    {
        return 0u;
    }

    value = SecuritySystem_JsonGetInt(json, "audio_medium_ratio_pct", s_status.audio_medium_ratio_pct, &found);
    if ((found != 0u) &&
        (SecuritySystem_ClampAndSetInt(value, SECURITY_PARAM_AUDIO_MEDIUM_MIN, SECURITY_PARAM_AUDIO_MEDIUM_MAX, &s_status.audio_medium_ratio_pct) == 0u))
    {
        return 0u;
    }

    value = SecuritySystem_JsonGetInt(json, "audio_strong_ratio_pct", s_status.audio_strong_ratio_pct, &found);
    if ((found != 0u) &&
        (SecuritySystem_ClampAndSetInt(value, SECURITY_PARAM_AUDIO_STRONG_MIN, SECURITY_PARAM_AUDIO_STRONG_MAX, &s_status.audio_strong_ratio_pct) == 0u))
    {
        return 0u;
    }

    SecuritySystem_SetConfigMirror();
    return 1u;
}

static void SecuritySystem_HandleTcpParseError(uint8_t link_id, uint32_t seq, TcpProtocol_ErrorCode_t error)
{
    if ((link_id >= TCP_PROTOCOL_MAX_LINKS) || (seq == 0u) || (error == TCP_ERR_BAD_MAGIC))
    {
        return;
    }

    SecuritySystem_SendError(link_id, seq, error, "parser");
}

static void SecuritySystem_HandleTcpMessage(const TcpProtocol_Message_t *message)
{
    char command[24];

    if ((message == NULL) || (message->link_id >= TCP_PROTOCOL_MAX_LINKS))
    {
        return;
    }

    s_tcp_clients[message->link_id].last_seen_tick = HAL_GetTick();
    s_tcp_clients[message->link_id].last_rx_type = (uint16_t)message->type;

    if (message->type == TCP_MSG_LOGIN_REQ)
    {
        int written;
        SecuritySystem_SelectTcpClient(message->link_id);
        s_tcp_clients[message->link_id].logged_in = 1u;
        s_status.active_clients = SecuritySystem_CountLoggedInClients();
        written = snprintf(
            s_tcp_body,
            sizeof(s_tcp_body),
            "{\"ok\":true,\"device_id\":\"%s\",\"protocol\":%u,\"heartbeat_ms\":10000,"
            "\"server_ip\":\"%s\",\"server_port\":%s}",
            SECURITY_TCP_DEVICE_ID,
            (unsigned int)TCP_PROTOCOL_VERSION,
            ESP_AP_IP,
            ESP_SERVER_PORT);
        if ((written > 0) && ((uint32_t)written < sizeof(s_tcp_body)))
        {
            if (SecuritySystem_SendTcpFrame(message->link_id, TCP_MSG_LOGIN_RSP, message->seq, s_tcp_body) == 0u)
            {
                s_tcp_clients[message->link_id].logged_in = 0u;
                SecuritySystem_RestartTcpServer();
                return;
            }
        }
        s_last_status_push_tick = HAL_GetTick();
        SecuritySystem_BroadcastStatus();
        return;
    }

    if (message->type == TCP_MSG_PING)
    {
        int written = snprintf(s_tcp_body, sizeof(s_tcp_body), "{\"ok\":true,\"tick\":%lu}", (unsigned long)HAL_GetTick());
        if ((written > 0) && ((uint32_t)written < sizeof(s_tcp_body)))
        {
            (void)SecuritySystem_SendTcpFrame(message->link_id, TCP_MSG_PONG, message->seq, s_tcp_body);
        }
        return;
    }

    if (SecuritySystem_IsLoggedIn(message->link_id) == 0u)
    {
        SecuritySystem_SendError(message->link_id, message->seq, TCP_ERR_NOT_LOGIN, "login required");
        return;
    }

    switch (message->type)
    {
    case TCP_MSG_STATUS_QUERY:
        SecuritySystem_SendStatus(message->link_id, TCP_MSG_STATUS_RSP, message->seq);
        break;

    case TCP_MSG_CONFIG_SET:
        if (SecuritySystem_ApplyDetectionParams(message->json) == 0u)
        {
            SecuritySystem_SendError(message->link_id, message->seq, TCP_ERR_INVALID_VALUE, "config range");
        }
        else
        {
            if (SecuritySystem_BuildStatusJson(s_tcp_body, sizeof(s_tcp_body)) != 0u)
            {
                (void)SecuritySystem_SendTcpFrame(message->link_id, TCP_MSG_CONFIG_RSP, message->seq, s_tcp_body);
            }
            SecuritySystem_BroadcastStatus();
        }
        break;

    case TCP_MSG_CONTROL_CMD:
    {
        uint8_t defer_broadcast = 0u;

        if (SecuritySystem_JsonGetString(message->json, "cmd", command, sizeof(command)) == 0u)
        {
            SecuritySystem_SendError(message->link_id, message->seq, TCP_ERR_INVALID_FIELD, "missing cmd");
            break;
        }

        if (strcmp(command, "arm") == 0)
        {
            SecuritySystem_SetArmed(1u);
        }
        else if (strcmp(command, "disarm") == 0)
        {
            SecuritySystem_SetArmed(0u);
        }
        else if (strcmp(command, "silence") == 0)
        {
            SecuritySystem_ActionSilence();
        }
        else if (strcmp(command, "clear_alarm") == 0)
        {
            SecuritySystem_ActionClear();
            defer_broadcast = 1u;
        }
        else
        {
            SecuritySystem_SendError(message->link_id, message->seq, TCP_ERR_INVALID_VALUE, "unknown cmd");
            break;
        }

        if (SecuritySystem_BuildStatusJson(s_tcp_body, sizeof(s_tcp_body)) != 0u)
        {
            (void)SecuritySystem_SendTcpFrame(message->link_id, TCP_MSG_CONTROL_RSP, message->seq, s_tcp_body);
        }
        s_last_status_push_tick = HAL_GetTick();
        if (defer_broadcast == 0u)
        {
            SecuritySystem_BroadcastStatus();
        }
        break;
    }

    case TCP_MSG_HISTORY_QUERY:
        SecuritySystem_SendHistory(message->link_id, message->seq);
        break;

    default:
        SecuritySystem_SendError(message->link_id, message->seq, TCP_ERR_UNKNOWN_TYPE, TcpProtocol_MessageTypeText(message->type));
        break;
    }
}

static void SecuritySystem_RestartTcpServer(void)
{
    uint8_t link_id;

    for (link_id = 0u; link_id < TCP_PROTOCOL_MAX_LINKS; link_id++)
    {
        SecuritySystem_ResetTcpClient(link_id);
    }

    s_tcp_active_link = SECURITY_TCP_NO_ACTIVE_LINK;
    s_last_link_mask = 0u;
    s_status.active_clients = 0u;
    s_status_push_pending = 0u;
    s_audio_report_pending = 0u;
    s_vibration_report_pending = 0u;
    s_alarm_report_pending = 0u;

    if (ESP_RestartServer() == ESP_OK)
    {
        return;
    }

    s_status.wifi_ready = 0u;
}

static void SecuritySystem_OnArmedToggle(void)
{
    SecuritySystem_SetArmed(s_armed_toggle);
}

static void SecuritySystem_OnAlarmHoldChanged(int32_t new_value)
{
    s_status.alarm_hold_ms = new_value;
}

static void SecuritySystem_OnFusionWindowChanged(int32_t new_value)
{
    s_status.fusion_window_ms = new_value;
}

static void SecuritySystem_OnAudioMediumRatioChanged(int32_t new_value)
{
    s_status.audio_medium_ratio_pct = new_value;
}

static void SecuritySystem_OnAudioStrongRatioChanged(int32_t new_value)
{
    s_status.audio_strong_ratio_pct = new_value;
}

static void SecuritySystem_ActionShowStatus(void)
{
    s_ui_page = UI_PAGE_STATUS;
    s_force_render = 1u;
}

static void SecuritySystem_ActionShowWifi(void)
{
    s_ui_page = UI_PAGE_WIFI;
    s_force_render = 1u;
}

static void SecuritySystem_ActionSilence(void)
{
    if (s_status.state == SECURITY_STATE_ALARM_LATCHED)
    {
        s_status.silenced = 1u;
        s_force_render = 1u;
    }
}

static void SecuritySystem_ActionClear(void)
{
    SecuritySystem_ClearAlarm();
}

static void SecuritySystem_RenderStatusPage(void)
{
    /*
     * Page mapping:
     * State/Arm/Sil/Alarm -> state machine outputs
     * A:*               -> MAX4466 audio features
     * V:*               -> PZT vibration features
     */
    OLED_Clear();

    OLED_ShowString(0, 0, "Real-time Status", OLED_6X8);

    snprintf(s_line_buffer, sizeof(s_line_buffer), "State:%s", SecuritySystem_StateText(s_status.state));
    OLED_ShowString(0, 8, s_line_buffer, OLED_6X8);

    snprintf(s_line_buffer, sizeof(s_line_buffer), "Arm:%u Sil:%u", (unsigned int)s_status.armed, (unsigned int)s_status.silenced);
    OLED_ShowString(0, 16, s_line_buffer, OLED_6X8);

    snprintf(s_line_buffer, sizeof(s_line_buffer), "A:%uHz E:%lu",
             (unsigned int)s_status.audio_features.dominant_freq_hz,
             (unsigned long)s_status.audio_features.total_energy);
    OLED_ShowString(0, 24, s_line_buffer, OLED_6X8);

    snprintf(s_line_buffer, sizeof(s_line_buffer), "A rms:%lu S:%u",
             (unsigned long)s_status.audio_features.rms,
             (unsigned int)s_status.audio_score);
    OLED_ShowString(0, 32, s_line_buffer, OLED_6X8);

    snprintf(s_line_buffer, sizeof(s_line_buffer), "V:%uHz P:%lu",
             (unsigned int)s_status.vibration_features.dominant_freq_hz,
             (unsigned long)s_status.vibration_features.peak_mv);
    OLED_ShowString(0, 40, s_line_buffer, OLED_6X8);

    snprintf(s_line_buffer, sizeof(s_line_buffer), "V e:%lu S:%u",
             (unsigned long)s_status.vibration_features.energy,
             (unsigned int)s_status.vibration_score);
    OLED_ShowString(0, 48, s_line_buffer, OLED_6X8);

    snprintf(s_line_buffer, sizeof(s_line_buffer), "Alarm:%s", s_status.last_alarm_reason);
    OLED_ShowString(0, 56, s_line_buffer, OLED_6X8);

    OLED_Update();
}

static void SecuritySystem_RenderWifiPage(void)
{
    OLED_Clear();

    OLED_ShowString(0, 0, "ESP AP Info", OLED_6X8);

    snprintf(s_line_buffer, sizeof(s_line_buffer), "SSID:%s", ESP_WIFI_SSID);
    OLED_ShowString(0, 8, s_line_buffer, OLED_6X8);

    snprintf(s_line_buffer, sizeof(s_line_buffer), "PASS:%s", ESP_WIFI_PASS);
    OLED_ShowString(0, 16, s_line_buffer, OLED_6X8);

    snprintf(s_line_buffer, sizeof(s_line_buffer), "IP:%s", ESP_AP_IP);
    OLED_ShowString(0, 24, s_line_buffer, OLED_6X8);

    snprintf(s_line_buffer, sizeof(s_line_buffer), "PORT:%s", ESP_SERVER_PORT);
    OLED_ShowString(0, 32, s_line_buffer, OLED_6X8);

    snprintf(s_line_buffer, sizeof(s_line_buffer), "Ready:%u Step:%u",
             (unsigned int)s_status.wifi_ready,
             (unsigned int)ESP_GetLastErrorStep());
    OLED_ShowString(0, 40, s_line_buffer, OLED_6X8);

    snprintf(s_line_buffer, sizeof(s_line_buffer), "Clients:%u Baud:%lu",
             (unsigned int)s_status.active_clients,
             (unsigned long)ESP_GetActiveBaud());
    OLED_ShowString(0, 48, s_line_buffer, OLED_6X8);

    OLED_ShowString(0, 56, "BACK:Menu", OLED_6X8);
    OLED_Update();
}

static void SecuritySystem_HandleUi(void)
{
    KeyEvent_t key = Key_Scan();
    uint32_t now_tick = HAL_GetTick();

    if (s_ui_page == UI_PAGE_MENU)
    {
        if (key != KEY_NONE)
        {
            MF_Process(key);
            s_force_render = 1u;
        }
    }
    else
    {
        if ((key == KEY_BACK) || (key == KEY_ENTER))
        {
            s_ui_page = UI_PAGE_MENU;
            s_force_render = 1u;
        }
    }

    if (s_ui_page == UI_PAGE_STATUS)
    {
        if ((now_tick - s_last_menu_render_tick) < SECURITY_STATUS_RENDER_MIN_MS)
        {
            return;
        }
    }
    else if (s_force_render == 0u)
    {
        if ((now_tick - s_last_menu_render_tick) < SECURITY_MENU_REFRESH_MS)
        {
            return;
        }
    }

    s_last_menu_render_tick = now_tick;
    s_force_render = 0u;

    if (s_ui_page == UI_PAGE_MENU)
    {
        MF_Render();
    }
    else if (s_ui_page == UI_PAGE_STATUS)
    {
        SecuritySystem_RenderStatusPage();
    }
    else
    {
        SecuritySystem_RenderWifiPage();
    }
}

static void SecuritySystem_BuildMenus(void)
{
    s_root_menu = MF_CreateMenu("Security");
    s_param_menu = MF_CreateMenu("Params");

    if ((s_root_menu == NULL) || (s_param_menu == NULL))
    {
        return;
    }

    MF_AddAction(s_root_menu, "Status Page", SecuritySystem_ActionShowStatus);
    MF_AddToggle(s_root_menu, "Armed", &s_armed_toggle, SecuritySystem_OnArmedToggle);
    MF_AddSubmenu(s_root_menu, "Params", s_param_menu);
    MF_AddAction(s_root_menu, "WiFi Page", SecuritySystem_ActionShowWifi);
    MF_AddAction(s_root_menu, "Silence", SecuritySystem_ActionSilence);
    MF_AddAction(s_root_menu, "Clear Alarm", SecuritySystem_ActionClear);

    MF_AddValue(s_param_menu, "Hold ms", &s_alarm_hold_setting, 3000, 60000, 1000, "", SecuritySystem_OnAlarmHoldChanged);
    MF_AddValue(s_param_menu, "Fusion ms", &s_fusion_window_setting, 100, 1000, 50, "", SecuritySystem_OnFusionWindowChanged);
    MF_AddValue(s_param_menu, "Audio mid%", &s_audio_medium_ratio_setting, 20, 90, 5, "", SecuritySystem_OnAudioMediumRatioChanged);
    MF_AddValue(s_param_menu, "Audio hi%", &s_audio_strong_ratio_setting, 30, 95, 5, "", SecuritySystem_OnAudioStrongRatioChanged);
}

static void SecuritySystem_ADC_Init(void)
{
    ADC_ChannelConfTypeDef channel_config;

    __HAL_RCC_ADC_CONFIG(RCC_ADCPCLK2_DIV6);

    memset(&s_hadc1, 0, sizeof(s_hadc1));
    s_hadc1.Instance = ADC1;
    s_hadc1.Init.ScanConvMode = ADC_SCAN_ENABLE;
    s_hadc1.Init.ContinuousConvMode = DISABLE;
    s_hadc1.Init.DiscontinuousConvMode = DISABLE;
    s_hadc1.Init.ExternalTrigConv = ADC_EXTERNALTRIGCONV_T3_TRGO;
    s_hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    s_hadc1.Init.NbrOfConversion = 2u;

    if (HAL_ADC_Init(&s_hadc1) != HAL_OK)
    {
        while (1)
        {
        }
    }

    memset(&channel_config, 0, sizeof(channel_config));
    channel_config.Channel = MAX4466_ADC_CHANNEL;
    channel_config.Rank = ADC_REGULAR_RANK_1;
    channel_config.SamplingTime = ADC_SAMPLETIME_71CYCLES_5;
    if (HAL_ADC_ConfigChannel(&s_hadc1, &channel_config) != HAL_OK)
    {
        while (1)
        {
        }
    }

    channel_config.Channel = PZT_ADC_CHANNEL;
    channel_config.Rank = ADC_REGULAR_RANK_2;
    if (HAL_ADC_ConfigChannel(&s_hadc1, &channel_config) != HAL_OK)
    {
        while (1)
        {
        }
    }

    if (HAL_ADCEx_Calibration_Start(&s_hadc1) != HAL_OK)
    {
        while (1)
        {
        }
    }
}

static void SecuritySystem_TIM3_Init(void)
{
    TIM_MasterConfigTypeDef master_config;

    memset(&s_htim3, 0, sizeof(s_htim3));
    s_htim3.Instance = TIM3;
    s_htim3.Init.Prescaler = 71u;
    s_htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
    s_htim3.Init.Period = 124u;
    s_htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    s_htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    if (HAL_TIM_Base_Init(&s_htim3) != HAL_OK)
    {
        while (1)
        {
        }
    }

    memset(&master_config, 0, sizeof(master_config));
    master_config.MasterOutputTrigger = TIM_TRGO_UPDATE;
    master_config.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;

    if (HAL_TIMEx_MasterConfigSynchronization(&s_htim3, &master_config) != HAL_OK)
    {
        while (1)
        {
        }
    }
}

static void SecuritySystem_StartSampling(void)
{
    if (HAL_ADC_Start_DMA(&s_hadc1, (uint32_t *)s_adc_dma_buffer, SECURITY_DMA_BUFFER_WORDS) != HAL_OK)
    {
        while (1)
        {
        }
    }

    if (HAL_TIM_Base_Start(&s_htim3) != HAL_OK)
    {
        while (1)
        {
        }
    }
}

void HAL_ADC_MspInit(ADC_HandleTypeDef *hadc)
{
    GPIO_InitTypeDef gpio_init;

    if ((hadc == NULL) || (hadc->Instance != ADC1))
    {
        return;
    }

    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();

    memset(&gpio_init, 0, sizeof(gpio_init));
    gpio_init.Pin = MAX4466_AO_GPIO_PIN | PZT_GPIO_PIN;
    gpio_init.Mode = GPIO_MODE_ANALOG;
    HAL_GPIO_Init(GPIOA, &gpio_init);

    memset(&s_hdma_adc1, 0, sizeof(s_hdma_adc1));
    s_hdma_adc1.Instance = DMA1_Channel1;
    s_hdma_adc1.Init.Direction = DMA_PERIPH_TO_MEMORY;
    s_hdma_adc1.Init.PeriphInc = DMA_PINC_DISABLE;
    s_hdma_adc1.Init.MemInc = DMA_MINC_ENABLE;
    s_hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    s_hdma_adc1.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    s_hdma_adc1.Init.Mode = DMA_CIRCULAR;
    s_hdma_adc1.Init.Priority = DMA_PRIORITY_HIGH;

    if (HAL_DMA_Init(&s_hdma_adc1) != HAL_OK)
    {
        while (1)
        {
        }
    }

    __HAL_LINKDMA(hadc, DMA_Handle, s_hdma_adc1);

    HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 1u, 0u);
    HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);
}

void HAL_TIM_Base_MspInit(TIM_HandleTypeDef *htim)
{
    if ((htim != NULL) && (htim->Instance == TIM3))
    {
        __HAL_RCC_TIM3_CLK_ENABLE();
    }
}

void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
    if ((hadc != NULL) && (hadc->Instance == ADC1))
    {
        s_adc_ready_flags |= 0x01u;
    }
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if ((hadc != NULL) && (hadc->Instance == ADC1))
    {
        s_adc_ready_flags |= 0x02u;
    }
}

void SecuritySystem_Init(void)
{
    memset(&s_status, 0, sizeof(s_status));
    SecuritySystem_SetLastReason("NONE");

    s_status.alarm_hold_ms = s_alarm_hold_setting;
    s_status.fusion_window_ms = s_fusion_window_setting;
    s_status.audio_medium_ratio_pct = s_audio_medium_ratio_setting;
    s_status.audio_strong_ratio_pct = s_audio_strong_ratio_setting;

    OLED_Init();
    Key_Init();
    Buzzer_Init();
    SecuritySystem_LED_Init();
    MAX4466_Init();
    PZT_Sensor_Init();

    MF_Reset();
    SecuritySystem_BuildMenus();
    if (s_root_menu != NULL)
    {
        MF_Start(s_root_menu);
    }

    TcpProtocol_Init(SecuritySystem_HandleTcpMessage, SecuritySystem_HandleTcpParseError);
    ESP_RegisterCallback(SecuritySystem_HandleEspPacket);
    ESP_RegisterLinkEventCallback(SecuritySystem_HandleEspLinkEvent);
    s_status.wifi_ready = (ESP_Init() == ESP_OK) ? 1u : 0u;
    s_last_esp_retry_tick = HAL_GetTick();

    SecuritySystem_SetArmed(1u);
    SecuritySystem_ADC_Init();
    SecuritySystem_TIM3_Init();
    SecuritySystem_StartSampling();
    s_force_render = 1u;
}

void SecuritySystem_Task(void)
{
    ESP_Process();
    SecuritySystem_MaintainState();
    SecuritySystem_UpdateOutputs();

    if (s_status.wifi_ready == 0u)
    {
        uint32_t now_tick = HAL_GetTick();
        if ((now_tick - s_last_esp_retry_tick) >= SECURITY_ESP_RETRY_MS)
        {
            s_last_esp_retry_tick = now_tick;
            s_status.wifi_ready = (ESP_Init() == ESP_OK) ? 1u : 0u;
            s_force_render = 1u;
        }
    }

    SecuritySystem_ProcessPendingFrames();
    ESP_Process();
    SecuritySystem_MaintainTcpClients();
    s_status.active_clients = SecuritySystem_CountLoggedInClients();
    SecuritySystem_HandleUi();
}

void SecuritySystem_DMA_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&s_hdma_adc1);
}

const SecuritySystem_Status_t *SecuritySystem_GetStatus(void)
{
    return &s_status;
}

const char *SecuritySystem_StateText(SecuritySystem_State_t state)
{
    switch (state)
    {
    case SECURITY_STATE_DISARMED:
        return "DISARMED";
    case SECURITY_STATE_WARMUP:
        return "WARMUP";
    case SECURITY_STATE_ARMED:
        return "ARMED";
    case SECURITY_STATE_SUSPICIOUS:
        return "SUSPICIOUS";
    case SECURITY_STATE_ALARM_LATCHED:
        return "ALARM";
    default:
        return "UNKNOWN";
    }
}
