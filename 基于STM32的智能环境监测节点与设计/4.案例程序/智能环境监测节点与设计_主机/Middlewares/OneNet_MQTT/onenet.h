#ifndef _ONENET_H_
#define _ONENET_H_

#include <stdint.h>

typedef struct
{
    int32_t temperature;
    int32_t humidity;
    int32_t pm25;
    int32_t mq135_mv;
    int32_t gas_percent;
    int32_t light_lux;
    int32_t valid_bits;
    uint8_t slave_online;
    uint8_t alarm_active;
    int32_t alarm_mask;
    int32_t temperature_threshold;
    int32_t humidity_threshold;
    int32_t pm25_threshold;
    int32_t gas_threshold;
    int32_t light_threshold;
} OneNetTelemetry_t;

typedef struct
{
    uint8_t has_temperature_threshold;
    int32_t temperature_threshold;
    uint8_t has_humidity_threshold;
    int32_t humidity_threshold;
    uint8_t has_pm25_threshold;
    int32_t pm25_threshold;
    uint8_t has_gas_threshold;
    int32_t gas_threshold;
    uint8_t has_light_threshold;
    int32_t light_threshold;
} OneNetThresholdUpdate_t;

typedef void (*OneNet_ThresholdCallback_t)(const OneNetThresholdUpdate_t *update);

uint8_t OneNet_Init(void);
void OneNet_RegisterThresholdCallback(OneNet_ThresholdCallback_t cb);
void OneNet_Process(void);
uint8_t OneNet_IsConnected(void);
uint8_t OneNet_PublishTelemetry(const OneNetTelemetry_t *data);
void OneNet_UpdateTelemetryCache(const OneNetTelemetry_t *data);
void OneNet_SetReportInterval(uint32_t interval_ms);

#endif
