#ifndef __PET_FEEDER_H
#define __PET_FEEDER_H

#include "stm32f1xx_hal.h"
#include "Hardware/Actuators/Actuators.h"
#include "Hardware/HC_SR04/HC_SR04.h"
#include <stdint.h>

#define PETFEEDER_UPLOAD_RECORD_COUNT 5U

typedef struct
{
    uint8_t dht11_init_ok;
    uint8_t dht11_status;
    uint8_t dht11_valid;
    int32_t temperature_c;
    int32_t humidity_pct;

    uint8_t adc_valid;
    uint16_t light_raw;
    int32_t illuminance_pct;
    uint16_t water_raw;
    int32_t water_level_pct;
    uint16_t air_raw;
    int32_t air_quality_pct;
    uint8_t air_do;

    HC_SR04_Status_t distance_status;
    uint8_t distance_valid;
    float pet_distance_cm;
    uint16_t pet_distance_mm;
    uint8_t pet_near;

    uint8_t weight_valid;
    int32_t weight_raw;
    float food_weight_g;
} PetFeederSensorData_t;

typedef struct
{
    int32_t temp_low_c;
    int32_t temp_high_c;
    int32_t humi_low_pct;
    int32_t humi_high_pct;
    int32_t light_low_pct;
    int32_t air_high_pct;
    int32_t distance_threshold_cm;
    int32_t weight_low_g;
    int32_t water_low_pct;
} PetFeederThresholds_t;

typedef struct
{
    uint8_t fan_on;
    uint8_t humidifier_on;
    uint8_t light_on;
    uint8_t pump_on;
    uint8_t buzzer_on;
    uint8_t feeding;
    uint8_t voice_playing;

    uint8_t manual_fan;
    uint8_t manual_humidifier;
    uint8_t manual_light;
    uint8_t manual_pump;
    uint8_t manual_pump_override;
    uint8_t manual_buzzer;
    WaterPump_State_t pump_state;
} PetFeederActuatorState_t;

typedef struct
{
    uint32_t tick_ms;
    uint8_t success;
    int32_t temperature_c;
    int32_t humidity_pct;
    int32_t illuminance_pct;
    int32_t air_quality_pct;
    float pet_distance_cm;
    uint8_t pet_near;
    float food_weight_g;
    int32_t water_level_pct;
} PetFeederUploadRecord_t;

void PetFeeder_Init(uint8_t dht11_init_ok);
void PetFeeder_Process(void);

const PetFeederSensorData_t *PetFeeder_GetSensorData(void);
PetFeederThresholds_t *PetFeeder_GetThresholds(void);
const PetFeederActuatorState_t *PetFeeder_GetActuatorState(void);
const PetFeederUploadRecord_t *PetFeeder_GetUploadRecords(void);
uint8_t PetFeeder_GetUploadRecordNextIndex(void);
uint32_t PetFeeder_GetUploadSuccessCount(void);
uint32_t PetFeeder_GetUploadFailCount(void);
uint8_t PetFeeder_IsOneNetInitOk(void);

#endif /* __PET_FEEDER_H */
