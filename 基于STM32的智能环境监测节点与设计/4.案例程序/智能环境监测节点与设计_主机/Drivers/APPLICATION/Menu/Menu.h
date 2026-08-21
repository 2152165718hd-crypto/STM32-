#ifndef __MENU_H__
#define __MENU_H__

#include "stm32f1xx_hal.h"
#include "onenet.h"

typedef struct
{
    uint8_t valid_bits;
    uint8_t temp_c;
    uint8_t hum_pct;
    uint16_t pm25_ugm3;
    uint16_t mq135_mv;
    uint16_t light_lux;
    uint32_t last_rx_tick;
    uint8_t has_report;
    uint8_t is_stale;
} HostSensorCache_t;

typedef struct
{
    int32_t pm25;
    int32_t mq135_pct;
    int32_t light_lux;
    int32_t temp_c;
    int32_t hum_pct;
} HostThresholds_t;

typedef struct
{
    uint8_t active_mask;
    uint8_t previous_mask;
    uint8_t auto_jump_pending;
} HostAlarmState_t;

void Menu_Init(void);
void Menu_Run(void);
void Menu_ApplyCloudThresholds(const OneNetThresholdUpdate_t *update);

const HostSensorCache_t *Menu_GetSensorCache(void);
const HostThresholds_t *Menu_GetThresholds(void);
const HostAlarmState_t *Menu_GetAlarmState(void);

#endif /* __MENU_H__ */
