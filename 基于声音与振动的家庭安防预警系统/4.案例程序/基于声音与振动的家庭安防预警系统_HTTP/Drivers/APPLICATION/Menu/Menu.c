#include ".\Application\Menu\Menu.h"
#include ".\Application\Menu\MenuFramework.h"
#include ".\Hardware\Buzzer\Buzzer.h"
#include ".\Hardware\ESP_01S\ESP_01S.h"
#include ".\Hardware\KEY\KEY.h"
#include ".\Hardware\OLED\OLED.h"

#include <stdio.h>
#include <string.h>

#define SECURITY_LED_PORT GPIOA
#define SECURITY_LED_PIN GPIO_PIN_6
#define SECURITY_LED_CLK_ENABLE() __HAL_RCC_GPIOA_CLK_ENABLE()

#define SECURITY_ADC_CHANNEL_COUNT 2u
#define SECURITY_DMA_FRAME_WORDS (MAX4466_FRAME_SAMPLES * SECURITY_ADC_CHANNEL_COUNT)
#define SECURITY_DMA_BUFFER_WORDS (SECURITY_DMA_FRAME_WORDS * 2u)
#define SECURITY_WARMUP_MS 3000u
#define SECURITY_MENU_REFRESH_MS 200u
#define SECURITY_ESP_RETRY_MS 5000u
#define SECURITY_SUSPICIOUS_BLINK_MS 500u
#define SECURITY_ALARM_BLINK_MS 125u
/* Floors keep thresholds robust when ambient baseline energy is very low. */
#define SECURITY_AUDIO_ENERGY_FLOOR 1500u
#define SECURITY_VIBRATION_ENERGY_FLOOR 200u
#define SECURITY_VIBRATION_STRONG_ENERGY_FLOOR 800u

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

static const char s_http_root_page[] =
    "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<meta http-equiv='Cache-Control' content='no-store,no-cache,must-revalidate,max-age=0'><meta http-equiv='Pragma' content='no-cache'>"
    "<link rel='icon' href='data:,'>"
    "<title>Home Security</title><style>"
    "body{font-family:Arial,sans-serif;margin:16px;background:#f3f4f6;color:#111}"
    ".card{background:#fff;padding:14px;border-radius:12px;box-shadow:0 2px 10px rgba(0,0,0,.08)}"
    "button{margin:4px 4px 4px 0;padding:10px 14px;border:0;border-radius:10px;background:#1f2937;color:#fff}"
    "pre{white-space:pre-wrap;font-size:14px;line-height:1.4}"
    "</style></head><body><div class='card'><h2>Home Security</h2><div id='headline'>Loading...</div>"
    "<div><button onclick=\"cmd('/arm')\">Arm</button><button onclick=\"cmd('/disarm')\">Disarm</button>"
    "<button onclick=\"cmd('/silence')\">Silence</button><button onclick=\"cmd('/clear')\">Clear</button></div>"
    "<pre id='detail'></pre></div><script>"
    "var loadBusy=0,loadTick=0;"
    "function req(p,cb){var x=new XMLHttpRequest(),done=0,timer=0;"
    "function finish(s,t){if(done){return;}done=1;if(timer){clearTimeout(timer);}cb(s,t);}"
    "timer=setTimeout(function(){try{x.abort();}catch(e){}finish(0,'');},1500);"
    "x.open('GET',p+(p.indexOf('?')>=0?'&':'?')+'t='+Date.now(),true);"
    "x.onreadystatechange=function(){if(x.readyState===4){finish(x.status,x.responseText);}};"
    "x.onerror=function(){finish(0,'');};"
    "try{x.send();}catch(e){finish(0,'');}}"
    "function render(d){"
    "document.getElementById('headline').textContent=d.state+' | armed='+d.armed+' | silenced='+d.silenced;"
    "document.getElementById('detail').textContent='Audio: '+d.audio_freq+' Hz, energy '+d.audio_energy+', ratio '+d.audio_ratio+'%\\n'"
    "+'Vibration: '+d.vib_freq+' Hz, peak '+d.vib_peak+' mV, energy '+d.vib_energy+', zeroX '+d.zero_cross+'\\n'"
    "+'Reason: '+d.reason;}"
    "function cmd(p){req(p,function(){setTimeout(load,200);});}"
    "function load(){if(loadBusy&&Date.now()-loadTick<1800){return;}loadBusy=1;loadTick=Date.now();req('/api/status',function(s,t){"
    "if(s===200){try{render(JSON.parse(t));}catch(e){}}loadBusy=0;});}"
    "setInterval(load,1000);setTimeout(load,400);</script></body></html>";

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
static char s_http_buffer[ESP_TX_BUF_SIZE];
static char s_http_body[512];
static char s_line_buffer[64];

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
static SignalLevel_t SecuritySystem_ClassifyAudio(void);
static SignalLevel_t SecuritySystem_ClassifyVibration(void);
static void SecuritySystem_ProcessDetection(SignalLevel_t audio_level, SignalLevel_t vibration_level);
static void SecuritySystem_ProcessFrame(const volatile uint16_t *frame_data);
static void SecuritySystem_ProcessPendingFrames(void);
static void SecuritySystem_UpdateOutputs(void);
static void SecuritySystem_MaintainState(void);
static void SecuritySystem_HandleEspPacket(ESP_DataPacket_t *packet);
static uint8_t SecuritySystem_CountBits(uint8_t value);
static uint8_t SecuritySystem_SendHttpChunks(uint8_t link_id, const uint8_t *data, uint16_t len);
static void SecuritySystem_SendHttpResponse(uint8_t link_id, const char *status, const char *content_type, const char *body);
static void SecuritySystem_SendJsonStatus(uint8_t link_id);
static void SecuritySystem_HandleHttpPath(uint8_t link_id, const char *path);
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
    s_status.last_alarm_tick = 0u;
    s_status.silenced = 0u;
    SecuritySystem_SetLastReason("NONE");
    SecuritySystem_ResetFusionHistory();

    if (s_status.armed != 0u)
    {
        SecuritySystem_SetState(SECURITY_STATE_ARMED);
    }
    else
    {
        SecuritySystem_SetState(SECURITY_STATE_DISARMED);
    }
}

static void SecuritySystem_TriggerAlarm(const char *reason)
{
    s_status.last_alarm_tick = HAL_GetTick();
    s_status.silenced = 0u;
    SecuritySystem_SetLastReason(reason);
    SecuritySystem_SetState(SECURITY_STATE_ALARM_LATCHED);
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

static SignalLevel_t SecuritySystem_ClassifyAudio(void)
{
    /* Relative thresholds from current baseline. */
    uint32_t medium_threshold = s_status.audio_baseline_energy * 5u / 2u;
    uint32_t strong_threshold = s_status.audio_baseline_energy * 5u;

    if (medium_threshold < SECURITY_AUDIO_ENERGY_FLOOR)
    {
        medium_threshold = SECURITY_AUDIO_ENERGY_FLOOR;
    }
    if (strong_threshold < SECURITY_AUDIO_ENERGY_FLOOR)
    {
        strong_threshold = SECURITY_AUDIO_ENERGY_FLOOR;
    }

    if ((s_status.audio_features.total_energy > strong_threshold) &&
        (s_status.audio_features.band_ratio_pct >= (uint16_t)s_status.audio_strong_ratio_pct))
    {
        /* Strong audio needs high total energy and high mid/high-band ratio. */
        return SIGNAL_LEVEL_STRONG;
    }

    if ((s_status.audio_features.total_energy > medium_threshold) &&
        (s_status.audio_features.band_ratio_pct >= (uint16_t)s_status.audio_medium_ratio_pct))
    {
        /* Medium audio uses looser but still dual-gated condition. */
        return SIGNAL_LEVEL_MEDIUM;
    }

    return SIGNAL_LEVEL_NONE;
}

static SignalLevel_t SecuritySystem_ClassifyVibration(void)
{
    /* Relative thresholds from vibration baseline amplitude and energy. */
    uint32_t peak_medium_threshold = s_status.vibration_baseline_peak_mv * 18u / 10u;
    uint32_t peak_strong_threshold = s_status.vibration_baseline_peak_mv * 3u;
    uint32_t energy_medium_threshold = s_status.vibration_baseline_energy * 2u;
    uint32_t energy_strong_threshold = s_status.vibration_baseline_energy * 35u / 10u;
    /* Frequency/zero-cross checks reject slow drift and offset-only disturbances. */
    uint8_t freq_valid = (s_status.vibration_features.dominant_freq_hz >= 31u) &&
                         (s_status.vibration_features.dominant_freq_hz <= 800u);
    uint8_t crossing_valid = (s_status.vibration_features.zero_cross_permille >= 80u);

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

    if ((crossing_valid != 0u) && (freq_valid != 0u) &&
        ((s_status.vibration_features.peak_mv > peak_strong_threshold) ||
         (s_status.vibration_features.energy > energy_strong_threshold)))
    {
        return SIGNAL_LEVEL_STRONG;
    }

    if ((crossing_valid != 0u) && (freq_valid != 0u) &&
        ((s_status.vibration_features.peak_mv > peak_medium_threshold) ||
         (s_status.vibration_features.energy > energy_medium_threshold)))
    {
        return SIGNAL_LEVEL_MEDIUM;
    }

    return SIGNAL_LEVEL_NONE;
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

    if (vibration_level == SIGNAL_LEVEL_STRONG)
    {
        /* Fast trigger path for violent vibration events. */
        SecuritySystem_TriggerAlarm("VIB_STRONG");
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

    if ((SecuritySystem_HasRecentAudioEvent(now_tick) != 0u) &&
        (SecuritySystem_HasRecentVibrationEvent(now_tick) != 0u))
    {
        /* Fusion trigger: audio and vibration medium events overlap in time window. */
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
    SignalLevel_t audio_level;
    SignalLevel_t vibration_level;
    uint16_t i = 0u;

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

    if ((s_status.state == SECURITY_STATE_WARMUP) || (s_status.state == SECURITY_STATE_ARMED))
    {
        audio_level = SecuritySystem_ClassifyAudio();
        vibration_level = SecuritySystem_ClassifyVibration();

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
        SecuritySystem_ProcessDetection(SecuritySystem_ClassifyAudio(), SecuritySystem_ClassifyVibration());
    }

    s_force_render = 1u;
}

static void SecuritySystem_ProcessPendingFrames(void)
{
    for (;;)
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

    if (s_status.armed == 0u)
    {
        SecuritySystem_SetState(SECURITY_STATE_DISARMED);
        return;
    }

    if ((s_status.state == SECURITY_STATE_WARMUP) &&
        ((now_tick - s_warmup_start_tick) >= SECURITY_WARMUP_MS))
    {
        /* Warmup completes after fixed settling period. */
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

static uint8_t SecuritySystem_CountBits(uint8_t value)
{
    uint8_t count = 0u;

    while (value != 0u)
    {
        count += (uint8_t)(value & 0x1u);
        value >>= 1;
    }

    return count;
}

static uint8_t SecuritySystem_SendHttpChunks(uint8_t link_id, const uint8_t *data, uint16_t len)
{
    uint16_t offset = 0u;
    uint16_t chunk_len = 0u;

    if ((data == NULL) && (len != 0u))
    {
        return 0u;
    }

    while (offset < len)
    {
        chunk_len = (uint16_t)(len - offset);
        if (chunk_len > ESP_TX_BUF_SIZE)
        {
            chunk_len = ESP_TX_BUF_SIZE;
        }

        if (ESP_SendData(link_id, &data[offset], chunk_len) != ESP_OK)
        {
            return 0u;
        }

        offset = (uint16_t)(offset + chunk_len);
    }

    return 1u;
}

static void SecuritySystem_SendHttpResponse(uint8_t link_id, const char *status, const char *content_type, const char *body)
{
    uint16_t body_len = 0u;
    uint16_t total_len = 0u;
    int written = 0;

    if ((status == NULL) || (content_type == NULL) || (body == NULL))
    {
        return;
    }

    body_len = (uint16_t)strlen(body);
    written = snprintf(
        s_http_buffer,
        sizeof(s_http_buffer),
        "HTTP/1.1 %s\r\nContent-Type: %s\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: %u\r\n\r\n",
        status,
        content_type,
        (unsigned int)body_len);

    if ((written > 0) && ((uint32_t)written < sizeof(s_http_buffer)))
    {
        total_len = (uint16_t)written + body_len;
        if ((uint32_t)total_len <= sizeof(s_http_buffer))
        {
            memcpy(&s_http_buffer[written], body, body_len);
            (void)ESP_SendData(link_id, (const uint8_t *)s_http_buffer, total_len);
        }
        else if (SecuritySystem_SendHttpChunks(link_id, (const uint8_t *)s_http_buffer, (uint16_t)written) != 0u)
        {
            (void)SecuritySystem_SendHttpChunks(link_id, (const uint8_t *)body, body_len);
        }
    }

    HAL_Delay(10u);
    (void)ESP_CloseLink(link_id);
}

static void SecuritySystem_SendJsonStatus(uint8_t link_id)
{
    /* Web status values are snapshots of the same fields used by OLED rendering. */
    int body_len = snprintf(
        s_http_body,
        sizeof(s_http_body),
        "{\"state\":\"%s\",\"armed\":%u,\"silenced\":%u,\"reason\":\"%s\",\"last_alarm_tick\":%lu,"
        "\"audio_freq\":%u,\"audio_energy\":%lu,\"audio_ratio\":%u,\"audio_rms\":%lu,"
        "\"vib_freq\":%u,\"vib_peak\":%lu,\"vib_energy\":%lu,\"zero_cross\":%u,"
        "\"wifi_ready\":%u,\"clients\":%u}",
        SecuritySystem_StateText(s_status.state),
        (unsigned int)s_status.armed,
        (unsigned int)s_status.silenced,
        s_status.last_alarm_reason,
        (unsigned long)s_status.last_alarm_tick,
        (unsigned int)s_status.audio_features.dominant_freq_hz,
        (unsigned long)s_status.audio_features.total_energy,
        (unsigned int)s_status.audio_features.band_ratio_pct,
        (unsigned long)s_status.audio_features.rms,
        (unsigned int)s_status.vibration_features.dominant_freq_hz,
        (unsigned long)s_status.vibration_features.peak_mv,
        (unsigned long)s_status.vibration_features.energy,
        (unsigned int)s_status.vibration_features.zero_cross_permille,
        (unsigned int)s_status.wifi_ready,
        (unsigned int)s_status.active_clients);

    if ((body_len > 0) && ((uint32_t)body_len < sizeof(s_http_body)))
    {
        SecuritySystem_SendHttpResponse(link_id, "200 OK", "application/json", s_http_body);
    }
}

static void SecuritySystem_HandleHttpPath(uint8_t link_id, const char *path)
{
    if (strcmp(path, "/") == 0)
    {
        SecuritySystem_SendHttpResponse(link_id, "200 OK", "text/html; charset=utf-8", s_http_root_page);
    }
    else if (strcmp(path, "/api/status") == 0)
    {
        SecuritySystem_SendJsonStatus(link_id);
    }
    else if (strcmp(path, "/favicon.ico") == 0)
    {
        SecuritySystem_SendHttpResponse(link_id, "204 No Content", "text/plain", "");
    }
    else if (strcmp(path, "/arm") == 0)
    {
        SecuritySystem_SetArmed(1u);
        SecuritySystem_SendHttpResponse(link_id, "200 OK", "text/plain", "ARMED");
    }
    else if (strcmp(path, "/disarm") == 0)
    {
        SecuritySystem_SetArmed(0u);
        SecuritySystem_SendHttpResponse(link_id, "200 OK", "text/plain", "DISARMED");
    }
    else if (strcmp(path, "/silence") == 0)
    {
        SecuritySystem_ActionSilence();
        SecuritySystem_SendHttpResponse(link_id, "200 OK", "text/plain", "SILENCED");
    }
    else if (strcmp(path, "/clear") == 0)
    {
        SecuritySystem_ActionClear();
        SecuritySystem_SendHttpResponse(link_id, "200 OK", "text/plain", "CLEARED");
    }
    else
    {
        SecuritySystem_SendHttpResponse(link_id, "404 Not Found", "text/plain", "NOT FOUND");
    }
}

static void SecuritySystem_HandleEspPacket(ESP_DataPacket_t *packet)
{
    char request[ESP_DATA_BUF_SIZE + 1u];
    char *path_start;
    char *path_end;
    char *path_trim;

    if ((packet == NULL) || (packet->len == 0u))
    {
        return;
    }

    memcpy(request, packet->data, packet->len);
    request[packet->len] = '\0';

    if (strncmp(request, "GET ", 4) != 0)
    {
        SecuritySystem_SendHttpResponse(packet->link_id, "405 Method Not Allowed", "text/plain", "GET ONLY");
        return;
    }

    path_start = request + 4;
    path_end = strchr(path_start, ' ');
    if (path_end == NULL)
    {
        SecuritySystem_SendHttpResponse(packet->link_id, "400 Bad Request", "text/plain", "BAD REQUEST");
        return;
    }

    *path_end = '\0';
    path_trim = strpbrk(path_start, "?#");
    if (path_trim != NULL)
    {
        *path_trim = '\0';
    }
    SecuritySystem_HandleHttpPath(packet->link_id, path_start);
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

    snprintf(s_line_buffer, sizeof(s_line_buffer), "A rms:%lu R:%u%%",
             (unsigned long)s_status.audio_features.rms,
             (unsigned int)s_status.audio_features.band_ratio_pct);
    OLED_ShowString(0, 32, s_line_buffer, OLED_6X8);

    snprintf(s_line_buffer, sizeof(s_line_buffer), "V:%uHz P:%lu",
             (unsigned int)s_status.vibration_features.dominant_freq_hz,
             (unsigned long)s_status.vibration_features.peak_mv);
    OLED_ShowString(0, 40, s_line_buffer, OLED_6X8);

    snprintf(s_line_buffer, sizeof(s_line_buffer), "V e:%lu Z:%u",
             (unsigned long)s_status.vibration_features.energy,
             (unsigned int)s_status.vibration_features.zero_cross_permille);
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

    if (s_force_render == 0u)
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
    s_htim3.Init.Period = 249u;
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

    ESP_RegisterCallback(SecuritySystem_HandleEspPacket);
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
    SecuritySystem_ProcessPendingFrames();
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

    ESP_Process();
    s_status.active_clients = SecuritySystem_CountBits(ESP_GetActiveLinkMask());
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
