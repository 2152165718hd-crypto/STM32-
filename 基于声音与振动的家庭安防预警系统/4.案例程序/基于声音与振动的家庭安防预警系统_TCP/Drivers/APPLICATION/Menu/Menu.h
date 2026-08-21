#ifndef __MENU_H
#define __MENU_H

#include "stm32f1xx_hal.h"

#include ".\Hardware\MAX4466\MAX4466.h"
#include ".\Hardware\PZT_Sensor\PZT_Sensor.h"

typedef enum
{
    SECURITY_STATE_DISARMED = 0,
    SECURITY_STATE_WARMUP,
    SECURITY_STATE_ARMED,
    SECURITY_STATE_SUSPICIOUS,
    SECURITY_STATE_ALARM_LATCHED
} SecuritySystem_State_t;

typedef struct
{
    uint8_t armed;
    uint8_t silenced;
    uint8_t wifi_ready;
    uint8_t active_clients;
    SecuritySystem_State_t state;
    uint32_t last_alarm_tick;
    char last_alarm_reason[24];
    MAX4466_Feature_t audio_features;
    PZT_Sensor_Feature_t vibration_features;
    uint8_t audio_score;
    uint8_t vibration_score;
    uint8_t audio_detect_score;
    uint8_t vibration_detect_score;
    uint32_t audio_baseline_energy;
    uint32_t vibration_baseline_peak_mv;
    uint32_t vibration_baseline_energy;
    int32_t alarm_hold_ms;
    int32_t fusion_window_ms;
    int32_t audio_medium_ratio_pct;
    int32_t audio_strong_ratio_pct;
} SecuritySystem_Status_t;

void SecuritySystem_Init(void);
void SecuritySystem_Task(void);
void SecuritySystem_DMA_IRQHandler(void);
const SecuritySystem_Status_t *SecuritySystem_GetStatus(void);
const char *SecuritySystem_StateText(SecuritySystem_State_t state);

#endif /* __MENU_H */
