#include "APPLICATION/PetFeeder/PetFeeder.h"

#include "APPLICATION/Menu/MenuFramework.h"
#include "Hardware/ADC_Sensors/ADC_Sensors.h"
#include "Hardware/AirM2M_4G/AirM2M_4G.h"
#include "Hardware/Buzzer/Buzzer.h"
#include "Hardware/DHT11/DHT11.h"
#include "Hardware/HX711_WeighingModule/HX711_WeighingModule.h"
#include "Hardware/JQ8400/JQ8400.h"
#include "Hardware/OLED/OLED.h"
#include "Hardware/Servo/Servo.h"
#include "onenet.h"
#include "onenet_config.h"

#include <stdio.h>
#include <string.h>

#define PF_DHT11_PERIOD_MS 1200U
#define PF_ADC_PERIOD_MS 500U
#define PF_DISTANCE_PERIOD_MS 250U
#define PF_WEIGHT_PERIOD_MS 500U
#define PF_MENU_RENDER_PERIOD_MS 100U
#define PF_UPLOAD_PERIOD_MS 2000U
#define PF_ONENET_RETRY_MS 30000U
#define PF_ONENET_START_DELAY_MS 10000U

#define PF_SERVO_STEP_MS 600U
#define PF_FEED_COOLDOWN_MS 20000U
#define PF_PUMP_MAX_RUN_MS 5000U

#define PF_LIGHT_DARK_RAW 3000U
#define PF_LIGHT_FULL_RAW 4095U
#define PF_PERCENT_MAX_RAW 4095U
#define PF_HYST_TEMP_C 2
#define PF_HYST_PERCENT 5

extern DHT11_Data_t DHT11_Data;

typedef enum
{
    PF_FEED_IDLE = 0,
    PF_FEED_AT_90,
    PF_FEED_AT_0,
    PF_FEED_AT_180
} PF_FeedStage_t;

static PetFeederSensorData_t s_sensor;
static PetFeederThresholds_t s_thresholds;
static PetFeederActuatorState_t s_actuator;
static PetFeederUploadRecord_t s_upload_records[PETFEEDER_UPLOAD_RECORD_COUNT];

static uint8_t s_upload_next_index = 0U;
static uint32_t s_upload_success_count = 0U;
static uint32_t s_upload_fail_count = 0U;

static uint8_t s_onenet_init_ok = 0U;
static uint32_t s_last_onenet_retry_ms = 0U;

static uint32_t s_last_dht11_ms = 0U;
static uint32_t s_last_adc_ms = 0U;
static uint32_t s_last_distance_ms = 0U;
static uint32_t s_last_weight_ms = 0U;
static uint32_t s_last_menu_render_ms = 0U;
static uint32_t s_last_upload_ms = 0U;
static uint8_t s_wait_service_active = 0U;

static PF_FeedStage_t s_feed_stage = PF_FEED_IDLE;
static uint32_t s_feed_stage_tick_ms = 0U;
static uint32_t s_last_feed_done_ms = 0U;
static uint32_t s_pump_start_ms = 0U;

static void PF_BuildMenu(void);
static void PF_ProcessSamples(uint32_t now);
static void PF_ProcessActuators(uint32_t now);
static void PF_ProcessFeed(uint32_t now);
static void PF_ProcessCloud(uint32_t now);
static void PF_ProcessMenu(uint32_t now);
static void PF_ServiceDuringWait(void);

static void PF_ActionFeedServo(void);
static void PF_ActionVoicePlay(void);
static void PF_ActionPumpAuto(void);
static void PF_TempLowChanged(int32_t new_value);
static void PF_TempHighChanged(int32_t new_value);
static void PF_HumiLowChanged(int32_t new_value);
static void PF_HumiHighChanged(int32_t new_value);
static void PF_ManualBuzzerChanged(void);
static void PF_ManualLightChanged(void);
static void PF_ManualHumidifierChanged(void);
static void PF_ManualPumpChanged(void);
static void PF_ManualFanChanged(void);

static void PF_PageTempHumi(KeyEvent_t key, uint8_t *exit_flag);
static void PF_PageLight(KeyEvent_t key, uint8_t *exit_flag);
static void PF_PageAir(KeyEvent_t key, uint8_t *exit_flag);
static void PF_PageDistance(KeyEvent_t key, uint8_t *exit_flag);
static void PF_PageWeight(KeyEvent_t key, uint8_t *exit_flag);
static void PF_PageWater(KeyEvent_t key, uint8_t *exit_flag);
static void PF_Page4GInfo(KeyEvent_t key, uint8_t *exit_flag);
static void PF_PageUploadRecords(KeyEvent_t key, uint8_t *exit_flag);

static uint8_t PF_Elapsed(uint32_t now, uint32_t last, uint32_t period)
{
    return (((uint32_t)(now - last)) >= period) ? 1U : 0U;
}

static int32_t PF_RawToPercent(uint16_t raw)
{
    uint32_t percent = ((uint32_t)raw * 100U + (PF_PERCENT_MAX_RAW / 2U)) / PF_PERCENT_MAX_RAW;

    if (percent > 100U)
    {
        percent = 100U;
    }

    return (int32_t)percent;
}

static int32_t PF_RawRangeToPercent(uint16_t raw, uint16_t raw_min, uint16_t raw_max)
{
    uint32_t range;
    uint32_t percent;

    if (raw_max <= raw_min)
    {
        return 0;
    }

    if (raw <= raw_min)
    {
        return 0;
    }
    if (raw >= raw_max)
    {
        return 100;
    }

    range = (uint32_t)raw_max - (uint32_t)raw_min;
    percent = (((uint32_t)raw - (uint32_t)raw_min) * 100U + (range / 2U)) / range;

    if (percent > 100U)
    {
        percent = 100U;
    }

    return (int32_t)percent;
}

static int32_t PF_WithFloor(int32_t value, int32_t delta, int32_t floor)
{
    if ((value - delta) < floor)
    {
        return floor;
    }

    return value - delta;
}

static int32_t PF_WithCeil(int32_t value, int32_t delta, int32_t ceil)
{
    if ((value + delta) > ceil)
    {
        return ceil;
    }

    return value + delta;
}

static uint32_t PF_FloatToScaledU32(float value, uint32_t scale)
{
    if (value <= 0.0f)
    {
        return 0U;
    }

    return (uint32_t)(value * (float)scale + 0.5f);
}

static const char *PF_OnOff(uint8_t on)
{
    return (on != 0U) ? "ON" : "OFF";
}

static const char *PF_DHT11StatusText(uint8_t status)
{
    if (status == DHT11_STATUS_OK)
    {
        return "OK";
    }
    if (status == DHT11_STATUS_NO_RESPONSE)
    {
        return "NOACK";
    }
    if (status == DHT11_STATUS_CHECKSUM_ERROR)
    {
        return "CHK";
    }
    if (status == DHT11_STATUS_TIMEOUT)
    {
        return "TIME";
    }
    if (status == DHT11_STATUS_RANGE_ERROR)
    {
        return "RANGE";
    }

    return "UNKN";
}

static const char *PF_HCSR04StatusText(HC_SR04_Status_t status)
{
    if (status == HC_SR04_OK)
    {
        return "OK";
    }
    if (status == HC_SR04_ERR_TIMEOUT_WAIT_HIGH)
    {
        return "TO_H";
    }
    if (status == HC_SR04_ERR_TIMEOUT_WAIT_LOW)
    {
        return "TO_L";
    }
    if (status == HC_SR04_ERR_NOT_INIT)
    {
        return "NINI";
    }
    if (status == HC_SR04_ERR_PARAM)
    {
        return "PARM";
    }

    return "UNKN";
}

static void PF_DrawPageHeader(const char *title)
{
    OLED_ShowString(0, 0, (char *)title, OLED_6X8);
    OLED_DrawLine(0, 9, 127, 9);
}

static void PF_ExitOnBack(KeyEvent_t key, uint8_t *exit_flag)
{
    if ((exit_flag != NULL) && (key == KEY_BACK))
    {
        *exit_flag = 1U;
    }
}

static void PF_SetFan(uint8_t on)
{
    s_actuator.fan_on = (on != 0U) ? 1U : 0U;
    Fan_Set(s_actuator.fan_on);
}

static void PF_SetHumidifier(uint8_t on)
{
    s_actuator.humidifier_on = (on != 0U) ? 1U : 0U;
    Humidifier_Set(s_actuator.humidifier_on);
}

static void PF_SetLight(uint8_t on)
{
    s_actuator.light_on = (on != 0U) ? 1U : 0U;
    LED_Set(s_actuator.light_on);
}

static void PF_SetBuzzer(uint8_t on)
{
    s_actuator.buzzer_on = (on != 0U) ? 1U : 0U;
    if (s_actuator.buzzer_on != 0U)
    {
        Buzzer_On();
    }
    else
    {
        Buzzer_Off();
    }
}

static void PF_StopPump(void)
{
    WaterPump_Stop();
    s_actuator.pump_on = 0U;
    s_actuator.pump_state = WATERPUMP_STATE_STOP;
    s_pump_start_ms = 0U;
}

static void PF_StartPump(uint32_t now)
{
    WaterPump_RunForward();
    s_actuator.pump_on = 1U;
    s_actuator.pump_state = WATERPUMP_STATE_FORWARD;
    s_pump_start_ms = now;
}

static uint8_t PF_CanAutoFeed(uint32_t now)
{
    if (s_feed_stage != PF_FEED_IDLE)
    {
        return 0U;
    }

    if (((uint32_t)(now - s_last_feed_done_ms)) < PF_FEED_COOLDOWN_MS)
    {
        return 0U;
    }

    return 1U;
}

static void PF_StartFeed(uint8_t force, uint32_t now)
{
    if ((force == 0U) && (PF_CanAutoFeed(now) == 0U))
    {
        return;
    }
    if (s_feed_stage != PF_FEED_IDLE)
    {
        return;
    }

    s_feed_stage = PF_FEED_AT_90;
    s_feed_stage_tick_ms = now;
    s_actuator.feeding = 1U;
    Servo_SetAngle(90U);
}

static void PF_RecordUpload(uint32_t now, uint8_t success)
{
    PetFeederUploadRecord_t *record = &s_upload_records[s_upload_next_index];

    record->tick_ms = now;
    record->success = success;
    record->temperature_c = s_sensor.temperature_c;
    record->humidity_pct = s_sensor.humidity_pct;
    record->illuminance_pct = s_sensor.illuminance_pct;
    record->air_quality_pct = s_sensor.air_quality_pct;
    record->pet_distance_cm = s_sensor.pet_distance_cm;
    record->pet_near = s_sensor.pet_near;
    record->food_weight_g = s_sensor.food_weight_g;
    record->water_level_pct = s_sensor.water_level_pct;

    s_upload_next_index++;
    if (s_upload_next_index >= PETFEEDER_UPLOAD_RECORD_COUNT)
    {
        s_upload_next_index = 0U;
    }

    if (success != 0U)
    {
        s_upload_success_count++;
    }
    else
    {
        s_upload_fail_count++;
    }
}

static void PF_ResetDefaults(uint8_t dht11_init_ok)
{
    memset(&s_sensor, 0, sizeof(s_sensor));
    memset(&s_actuator, 0, sizeof(s_actuator));
    memset(s_upload_records, 0, sizeof(s_upload_records));

    s_sensor.dht11_init_ok = dht11_init_ok;
    s_sensor.dht11_status = 1U;
    s_sensor.distance_status = HC_SR04_ERR_NOT_INIT;

    s_thresholds.temp_low_c = 18;
    s_thresholds.temp_high_c = 30;
    s_thresholds.humi_low_pct = 40;
    s_thresholds.humi_high_pct = 70;
    s_thresholds.light_low_pct = 30;
    s_thresholds.air_high_pct = 60;
    s_thresholds.distance_threshold_cm = 30;
    s_thresholds.weight_low_g = 50;
    s_thresholds.water_low_pct = 30;

    s_upload_next_index = 0U;
    s_upload_success_count = 0U;
    s_upload_fail_count = 0U;
    s_feed_stage = PF_FEED_IDLE;
    s_feed_stage_tick_ms = 0U;
    s_last_feed_done_ms = HAL_GetTick() - PF_FEED_COOLDOWN_MS;
    s_pump_start_ms = 0U;
}

void PetFeeder_Init(uint8_t dht11_init_ok)
{
    uint32_t now = HAL_GetTick();

    PF_ResetDefaults(dht11_init_ok);
    PF_BuildMenu();
    MF_Render();
    AirM2M_4G_RegisterWaitCallback(PF_ServiceDuringWait);

    s_last_dht11_ms = now - PF_DHT11_PERIOD_MS;
    s_last_adc_ms = now - PF_ADC_PERIOD_MS;
    s_last_distance_ms = now - PF_DISTANCE_PERIOD_MS;
    s_last_weight_ms = now - PF_WEIGHT_PERIOD_MS;
    s_last_menu_render_ms = now;
    s_last_upload_ms = now - PF_UPLOAD_PERIOD_MS;
    s_last_onenet_retry_ms = now - PF_ONENET_RETRY_MS + PF_ONENET_START_DELAY_MS;

    s_onenet_init_ok = 0U;
}

void PetFeeder_Process(void)
{
    uint32_t now = HAL_GetTick();

    PF_ProcessMenu(now);
    PF_ProcessSamples(now);
    PF_ProcessActuators(now);
    PF_ProcessFeed(now);
    PF_ProcessCloud(now);
}

static void PF_ProcessSamples(uint32_t now)
{
    if (PF_Elapsed(now, s_last_dht11_ms, PF_DHT11_PERIOD_MS) != 0U)
    {
        s_sensor.dht11_status = DHT11_ReadData();
        if (s_sensor.dht11_status == DHT11_STATUS_OK)
        {
            s_sensor.dht11_valid = 1U;
            s_sensor.temperature_c = (int32_t)DHT11_Data.temp_int;
            s_sensor.humidity_pct = (int32_t)DHT11_Data.humi_int;
        }
        else
        {
            s_sensor.dht11_valid = 0U;
        }
        s_last_dht11_ms = now;
    }

    if (PF_Elapsed(now, s_last_adc_ms, PF_ADC_PERIOD_MS) != 0U)
    {
        s_sensor.light_raw = LightSensor_ReadRaw();
        s_sensor.illuminance_pct = PF_RawRangeToPercent(s_sensor.light_raw,
                                                        PF_LIGHT_DARK_RAW,
                                                        PF_LIGHT_FULL_RAW);
        s_sensor.water_raw = WaterSensor_ReadRaw();
        s_sensor.water_level_pct = PF_RawToPercent(s_sensor.water_raw);
        s_sensor.air_raw = MQ135_ReadRaw();
        s_sensor.air_quality_pct = PF_RawToPercent(s_sensor.air_raw);
        s_sensor.air_do = MQ135_ReadDO();
        s_sensor.adc_valid = 1U;
        s_last_adc_ms = now;
    }

    if (PF_Elapsed(now, s_last_distance_ms, PF_DISTANCE_PERIOD_MS) != 0U)
    {
        uint32_t echo_us = 0U;
        s_sensor.distance_status = HC_SR04_ReadEchoTimeUs(&echo_us);
        if (s_sensor.distance_status == HC_SR04_OK)
        {
            uint32_t mm = (echo_us * 343U) / 2000U;
            if (mm > 65535U)
            {
                mm = 65535U;
            }
            s_sensor.distance_valid = 1U;
            s_sensor.pet_distance_cm = (float)echo_us / 58.0f;
            s_sensor.pet_distance_mm = (uint16_t)mm;
            s_sensor.pet_near = (s_sensor.pet_distance_cm <= (float)s_thresholds.distance_threshold_cm) ? 1U : 0U;
        }
        else
        {
            s_sensor.distance_valid = 0U;
            s_sensor.pet_distance_cm = 0.0f;
            s_sensor.pet_distance_mm = 0U;
            s_sensor.pet_near = 0U;
        }
        s_last_distance_ms = now;
    }

    if (PF_Elapsed(now, s_last_weight_ms, PF_WEIGHT_PERIOD_MS) != 0U)
    {
        int32_t raw = 0;
        float weight_g = 0.0f;

        if (HX711_ReadWeightNonBlocking(&weight_g, &raw) != 0U)
        {
            s_sensor.weight_raw = raw;
            s_sensor.food_weight_g = weight_g;
            s_sensor.weight_valid = 1U;
        }
        else
        {
            s_sensor.weight_valid = 0U;
        }
        s_last_weight_ms = now;
    }
}

static void PF_ProcessActuators(uint32_t now)
{
    if ((s_actuator.pump_on != 0U) && (s_pump_start_ms != 0U) &&
        (PF_Elapsed(now, s_pump_start_ms, PF_PUMP_MAX_RUN_MS) != 0U))
    {
        s_actuator.manual_pump = 0U;
        PF_StopPump();
    }

    if ((s_sensor.dht11_valid != 0U) && (s_actuator.manual_fan == 0U))
    {
        if ((s_actuator.fan_on == 0U) &&
            ((s_sensor.temperature_c > s_thresholds.temp_high_c) ||
             (s_sensor.humidity_pct > s_thresholds.humi_high_pct)))
        {
            PF_SetFan(1U);
        }
        else if ((s_actuator.fan_on != 0U) &&
                 (s_sensor.temperature_c < PF_WithFloor(s_thresholds.temp_high_c, PF_HYST_TEMP_C, 0)) &&
                 (s_sensor.humidity_pct < PF_WithFloor(s_thresholds.humi_high_pct, PF_HYST_PERCENT, 0)))
        {
            PF_SetFan(0U);
        }
    }

    if ((s_sensor.dht11_valid != 0U) && (s_actuator.manual_humidifier == 0U))
    {
        if ((s_actuator.humidifier_on == 0U) &&
            (s_sensor.humidity_pct < s_thresholds.humi_low_pct))
        {
            PF_SetHumidifier(1U);
        }
        else if ((s_actuator.humidifier_on != 0U) &&
                 (s_sensor.humidity_pct > PF_WithCeil(s_thresholds.humi_low_pct, PF_HYST_PERCENT, 100)))
        {
            PF_SetHumidifier(0U);
        }
    }

    if ((s_sensor.adc_valid != 0U) && (s_actuator.manual_light == 0U))
    {
        if ((s_actuator.light_on == 0U) &&
            (s_sensor.illuminance_pct < s_thresholds.light_low_pct))
        {
            PF_SetLight(1U);
        }
        else if ((s_actuator.light_on != 0U) &&
                 (s_sensor.illuminance_pct > PF_WithCeil(s_thresholds.light_low_pct, PF_HYST_PERCENT, 100)))
        {
            PF_SetLight(0U);
        }
    }

    if ((s_sensor.adc_valid != 0U) && (s_actuator.manual_buzzer == 0U))
    {
        if ((s_actuator.buzzer_on == 0U) &&
            (s_sensor.air_quality_pct > s_thresholds.air_high_pct))
        {
            PF_SetBuzzer(1U);
        }
        else if ((s_actuator.buzzer_on != 0U) &&
                 (s_sensor.air_quality_pct < PF_WithFloor(s_thresholds.air_high_pct, PF_HYST_PERCENT, 0)))
        {
            PF_SetBuzzer(0U);
        }
    }

    if ((s_sensor.adc_valid != 0U) && (s_actuator.manual_pump_override == 0U))
    {
        if ((s_actuator.pump_on == 0U) &&
            (s_sensor.water_level_pct < s_thresholds.water_low_pct))
        {
            PF_StartPump(now);
        }
        else if ((s_actuator.pump_on != 0U) &&
                 (s_sensor.water_level_pct >= PF_WithCeil(s_thresholds.water_low_pct, PF_HYST_PERCENT, 100)))
        {
            PF_StopPump();
        }
    }

    if ((s_sensor.distance_valid != 0U) && (s_sensor.weight_valid != 0U) &&
        (s_sensor.pet_near != 0U) &&
        (s_sensor.food_weight_g < (float)s_thresholds.weight_low_g))
    {
        PF_StartFeed(0U, now);
    }

    if ((s_actuator.voice_playing != 0U) && (JQ8400_IsBusy() == 0U))
    {
        s_actuator.voice_playing = 0U;
    }
}

static void PF_ProcessFeed(uint32_t now)
{
    if (s_feed_stage == PF_FEED_IDLE)
    {
        return;
    }

    if (PF_Elapsed(now, s_feed_stage_tick_ms, PF_SERVO_STEP_MS) == 0U)
    {
        return;
    }

    s_feed_stage_tick_ms = now;
    if (s_feed_stage == PF_FEED_AT_90)
    {
        Servo_SetAngle(0U);
        s_feed_stage = PF_FEED_AT_0;
    }
    else if (s_feed_stage == PF_FEED_AT_0)
    {
        Servo_SetAngle(180U);
        s_feed_stage = PF_FEED_AT_180;
    }
    else
    {
        Servo_SetAngle(90U);
        s_feed_stage = PF_FEED_IDLE;
        s_actuator.feeding = 0U;
        s_last_feed_done_ms = now;
    }
}

static void PF_ProcessCloud(uint32_t now)
{
    if (s_onenet_init_ok != 0U)
    {
        OneNet_Process();
    }
    else if (PF_Elapsed(now, s_last_onenet_retry_ms, PF_ONENET_RETRY_MS) != 0U)
    {
        s_last_onenet_retry_ms = now;
        s_onenet_init_ok = OneNet_Init();
    }

    if (PF_Elapsed(now, s_last_upload_ms, PF_UPLOAD_PERIOD_MS) != 0U)
    {
        OneNetProperty_t properties[ONENET_PETFEEDER_PROPERTY_COUNT];
        uint8_t success = 0U;

        memset(properties, 0, sizeof(properties));
        properties[0].name = ONENET_PROPERTY_TEMPERATURE;
        properties[0].type = ONENET_VALUE_INT;
        properties[0].value.i32 = s_sensor.temperature_c;

        properties[1].name = ONENET_PROPERTY_HUMIDITY;
        properties[1].type = ONENET_VALUE_INT;
        properties[1].value.i32 = s_sensor.humidity_pct;

        properties[2].name = ONENET_PROPERTY_ILLUMINANCE;
        properties[2].type = ONENET_VALUE_INT;
        properties[2].value.i32 = s_sensor.illuminance_pct;

        properties[3].name = ONENET_PROPERTY_AIR_QUALITY;
        properties[3].type = ONENET_VALUE_INT;
        properties[3].value.i32 = s_sensor.air_quality_pct;

        properties[4].name = ONENET_PROPERTY_PET_DISTANCE;
        properties[4].type = ONENET_VALUE_FLOAT;
        properties[4].value.f32 = s_sensor.pet_distance_cm;
        properties[4].precision = 1U;

        properties[5].name = ONENET_PROPERTY_PET_NEAR;
        properties[5].type = ONENET_VALUE_BOOL;
        properties[5].value.boolean = s_sensor.pet_near;

        properties[6].name = ONENET_PROPERTY_FOOD_WEIGHT;
        properties[6].type = ONENET_VALUE_FLOAT;
        properties[6].value.f32 = s_sensor.food_weight_g;
        properties[6].precision = 1U;

        properties[7].name = ONENET_PROPERTY_WATER_LEVEL;
        properties[7].type = ONENET_VALUE_INT;
        properties[7].value.i32 = s_sensor.water_level_pct;

        if (s_onenet_init_ok != 0U)
        {
            success = OneNet_PublishProperties(properties, ONENET_PETFEEDER_PROPERTY_COUNT);
        }

        PF_RecordUpload(now, success);
        s_last_upload_ms = now;
    }
}

static void PF_ProcessMenu(uint32_t now)
{
    KeyEvent_t key = Key_Scan();

    MF_Process(key);
    if ((key != KEY_NONE) || (PF_Elapsed(now, s_last_menu_render_ms, PF_MENU_RENDER_PERIOD_MS) != 0U))
    {
        MF_Render();
        s_last_menu_render_ms = now;
    }
}

static void PF_ServiceDuringWait(void)
{
    uint32_t now;

    if (s_wait_service_active != 0U)
    {
        return;
    }

    s_wait_service_active = 1U;
    now = HAL_GetTick();
    PF_ProcessSamples(now);
    PF_ProcessActuators(now);
    PF_ProcessFeed(now);
    PF_ProcessMenu(now);
    s_wait_service_active = 0U;
}

static void PF_BuildMenu(void)
{
    MF_Menu_t *root;
    MF_Menu_t *sensor;
    MF_Menu_t *threshold;
    MF_Menu_t *manual;
    MF_Menu_t *cloud;

    MF_Reset();
    root = MF_CreateMenu("Main");
    sensor = MF_CreateMenu("Sensor");
    threshold = MF_CreateMenu("Threshold");
    manual = MF_CreateMenu("Manual");
    cloud = MF_CreateMenu("4G Info");

    if ((root == NULL) || (sensor == NULL) || (threshold == NULL) ||
        (manual == NULL) || (cloud == NULL))
    {
        return;
    }

    MF_AddSubmenu(root, "Sensor", sensor);
    MF_AddSubmenu(root, "Threshold", threshold);
    MF_AddSubmenu(root, "Manual", manual);
    MF_AddSubmenu(root, "4G Info", cloud);

    MF_AddCustomPage(sensor, "Temp/Humi", PF_PageTempHumi);
    MF_AddCustomPage(sensor, "Light", PF_PageLight);
    MF_AddCustomPage(sensor, "Air", PF_PageAir);
    MF_AddCustomPage(sensor, "Distance", PF_PageDistance);
    MF_AddCustomPage(sensor, "Weight", PF_PageWeight);
    MF_AddCustomPage(sensor, "Water", PF_PageWater);

    MF_AddValue(threshold, "T Low", &s_thresholds.temp_low_c, 0, 80, 1, "C", PF_TempLowChanged);
    MF_AddValue(threshold, "T High", &s_thresholds.temp_high_c, 0, 100, 1, "C", PF_TempHighChanged);
    MF_AddValue(threshold, "H Low", &s_thresholds.humi_low_pct, 0, 100, 1, "%", PF_HumiLowChanged);
    MF_AddValue(threshold, "H High", &s_thresholds.humi_high_pct, 0, 100, 1, "%", PF_HumiHighChanged);
    MF_AddValue(threshold, "Light Low", &s_thresholds.light_low_pct, 0, 100, 1, "%", NULL);
    MF_AddValue(threshold, "Air High", &s_thresholds.air_high_pct, 0, 100, 1, "%", NULL);
    MF_AddValue(threshold, "Dist", &s_thresholds.distance_threshold_cm, 1, 300, 1, "cm", NULL);
    MF_AddValue(threshold, "Weight", &s_thresholds.weight_low_g, 0, 10000, 10, "g", NULL);
    MF_AddValue(threshold, "Water Low", &s_thresholds.water_low_pct, 0, 100, 1, "%", NULL);

    MF_AddAction(manual, "Feed Servo", PF_ActionFeedServo);
    MF_AddToggle(manual, "Buzzer MAN", &s_actuator.manual_buzzer, PF_ManualBuzzerChanged);
    MF_AddToggle(manual, "Light MAN", &s_actuator.manual_light, PF_ManualLightChanged);
    MF_AddToggle(manual, "Humid MAN", &s_actuator.manual_humidifier, PF_ManualHumidifierChanged);
    MF_AddToggle(manual, "Pump MAN", &s_actuator.manual_pump, PF_ManualPumpChanged);
    MF_AddAction(manual, "Pump AUTO", PF_ActionPumpAuto);
    MF_AddToggle(manual, "Fan MAN", &s_actuator.manual_fan, PF_ManualFanChanged);
    MF_AddAction(manual, "Voice Play", PF_ActionVoicePlay);

    MF_AddCustomPage(cloud, "Status", PF_Page4GInfo);
    MF_AddCustomPage(cloud, "Upload Log", PF_PageUploadRecords);

    MF_Start(root);
}

static void PF_ActionFeedServo(void)
{
    PF_StartFeed(1U, HAL_GetTick());
}

static void PF_ActionVoicePlay(void)
{
    JQ8400_PlayTrack(1U);
    s_actuator.voice_playing = 1U;
}

static void PF_ActionPumpAuto(void)
{
    s_actuator.manual_pump_override = 0U;
    s_actuator.manual_pump = s_actuator.pump_on;
}

static void PF_TempLowChanged(int32_t new_value)
{
    if (new_value > s_thresholds.temp_high_c)
    {
        s_thresholds.temp_high_c = new_value;
    }
}

static void PF_TempHighChanged(int32_t new_value)
{
    if (new_value < s_thresholds.temp_low_c)
    {
        s_thresholds.temp_low_c = new_value;
    }
}

static void PF_HumiLowChanged(int32_t new_value)
{
    if (new_value > s_thresholds.humi_high_pct)
    {
        s_thresholds.humi_high_pct = new_value;
    }
}

static void PF_HumiHighChanged(int32_t new_value)
{
    if (new_value < s_thresholds.humi_low_pct)
    {
        s_thresholds.humi_low_pct = new_value;
    }
}

static void PF_ManualBuzzerChanged(void)
{
    if (s_actuator.manual_buzzer != 0U)
    {
        PF_SetBuzzer(1U);
    }
    else
    {
        PF_SetBuzzer(0U);
    }
}

static void PF_ManualLightChanged(void)
{
    if (s_actuator.manual_light != 0U)
    {
        PF_SetLight(1U);
    }
    else
    {
        PF_SetLight(0U);
    }
}

static void PF_ManualHumidifierChanged(void)
{
    if (s_actuator.manual_humidifier != 0U)
    {
        PF_SetHumidifier(1U);
    }
    else
    {
        PF_SetHumidifier(0U);
    }
}

static void PF_ManualPumpChanged(void)
{
    s_actuator.manual_pump_override = 1U;
    if (s_actuator.manual_pump != 0U)
    {
        PF_StartPump(HAL_GetTick());
    }
    else
    {
        PF_StopPump();
    }
}

static void PF_ManualFanChanged(void)
{
    if (s_actuator.manual_fan != 0U)
    {
        PF_SetFan(1U);
    }
    else
    {
        PF_SetFan(0U);
    }
}

static void PF_PageTempHumi(KeyEvent_t key, uint8_t *exit_flag)
{
    PF_ExitOnBack(key, exit_flag);
    if ((exit_flag != NULL) && (*exit_flag != 0U))
    {
        return;
    }

    PF_DrawPageHeader("Temp/Humi");
    OLED_Printf(0, 12, OLED_6X8, "T:%ldC  [%ld,%ld]", (long)s_sensor.temperature_c,
                (long)s_thresholds.temp_low_c, (long)s_thresholds.temp_high_c);
    OLED_Printf(0, 22, OLED_6X8, "H:%ld%%  [%ld,%ld]", (long)s_sensor.humidity_pct,
                (long)s_thresholds.humi_low_pct, (long)s_thresholds.humi_high_pct);
    OLED_Printf(0, 32, OLED_6X8, "Init:%s Read:%s", s_sensor.dht11_init_ok ? "OK" : "FAIL",
                PF_DHT11StatusText(s_sensor.dht11_status));
    OLED_Printf(0, 42, OLED_6X8, "Fan:%s Humid:%s", PF_OnOff(s_actuator.fan_on),
                PF_OnOff(s_actuator.humidifier_on));
    OLED_ShowString(0, 54, "BACK:Menu", OLED_6X8);
}

static void PF_PageLight(KeyEvent_t key, uint8_t *exit_flag)
{
    PF_ExitOnBack(key, exit_flag);
    if ((exit_flag != NULL) && (*exit_flag != 0U))
    {
        return;
    }

    PF_DrawPageHeader("Light");
    OLED_Printf(0, 12, OLED_6X8, "Raw:%4u", (unsigned int)s_sensor.light_raw);
    OLED_Printf(0, 22, OLED_6X8, "Level:%3ld%%", (long)s_sensor.illuminance_pct);
    OLED_Printf(0, 32, OLED_6X8, "Low:%3ld%%", (long)s_thresholds.light_low_pct);
    OLED_Printf(0, 42, OLED_6X8, "Light:%s MAN:%s", PF_OnOff(s_actuator.light_on),
                PF_OnOff(s_actuator.manual_light));
    OLED_ShowString(0, 54, "BACK:Menu", OLED_6X8);
}

static void PF_PageAir(KeyEvent_t key, uint8_t *exit_flag)
{
    PF_ExitOnBack(key, exit_flag);
    if ((exit_flag != NULL) && (*exit_flag != 0U))
    {
        return;
    }

    PF_DrawPageHeader("Air");
    OLED_Printf(0, 12, OLED_6X8, "Raw:%4u DO:%u", (unsigned int)s_sensor.air_raw,
                (unsigned int)s_sensor.air_do);
    OLED_Printf(0, 22, OLED_6X8, "Air:%3ld%% High:%3ld%%", (long)s_sensor.air_quality_pct,
                (long)s_thresholds.air_high_pct);
    OLED_Printf(0, 32, OLED_6X8, "Buzzer:%s MAN:%s", PF_OnOff(s_actuator.buzzer_on),
                PF_OnOff(s_actuator.manual_buzzer));
    OLED_ShowString(0, 54, "BACK:Menu", OLED_6X8);
}

static void PF_PageDistance(KeyEvent_t key, uint8_t *exit_flag)
{
    uint32_t cm10 = PF_FloatToScaledU32(s_sensor.pet_distance_cm, 10U);

    PF_ExitOnBack(key, exit_flag);
    if ((exit_flag != NULL) && (*exit_flag != 0U))
    {
        return;
    }

    PF_DrawPageHeader("Distance");
    OLED_Printf(0, 12, OLED_6X8, "Stat:%s Near:%u", PF_HCSR04StatusText(s_sensor.distance_status),
                (unsigned int)s_sensor.pet_near);
    OLED_Printf(0, 22, OLED_6X8, "Dist:%lu.%lu cm", (unsigned long)(cm10 / 10U),
                (unsigned long)(cm10 % 10U));
    OLED_Printf(0, 32, OLED_6X8, "Dist:%u mm", (unsigned int)s_sensor.pet_distance_mm);
    OLED_Printf(0, 42, OLED_6X8, "Near if <=%ldcm", (long)s_thresholds.distance_threshold_cm);
    OLED_ShowString(0, 54, "BACK:Menu", OLED_6X8);
}

static void PF_PageWeight(KeyEvent_t key, uint8_t *exit_flag)
{
    uint32_t weight10 = PF_FloatToScaledU32(s_sensor.food_weight_g, 10U);

    PF_ExitOnBack(key, exit_flag);
    if ((exit_flag != NULL) && (*exit_flag != 0U))
    {
        return;
    }

    PF_DrawPageHeader("Weight");
    OLED_Printf(0, 12, OLED_6X8, "Raw:%ld", (long)s_sensor.weight_raw);
    OLED_Printf(0, 22, OLED_6X8, "Food:%lu.%lug", (unsigned long)(weight10 / 10U),
                (unsigned long)(weight10 % 10U));
    OLED_Printf(0, 32, OLED_6X8, "Low:%ldg %s", (long)s_thresholds.weight_low_g,
                s_sensor.weight_valid ? "OK" : "WAIT");
    OLED_Printf(0, 42, OLED_6X8, "Feeding:%s", PF_OnOff(s_actuator.feeding));
    OLED_ShowString(0, 54, "BACK:Menu", OLED_6X8);
}

static void PF_PageWater(KeyEvent_t key, uint8_t *exit_flag)
{
    PF_ExitOnBack(key, exit_flag);
    if ((exit_flag != NULL) && (*exit_flag != 0U))
    {
        return;
    }

    PF_DrawPageHeader("Water");
    OLED_Printf(0, 12, OLED_6X8, "Raw:%4u", (unsigned int)s_sensor.water_raw);
    OLED_Printf(0, 22, OLED_6X8, "Level:%3ld%%", (long)s_sensor.water_level_pct);
    OLED_Printf(0, 32, OLED_6X8, "Low:%3ld%%", (long)s_thresholds.water_low_pct);
    OLED_Printf(0, 42, OLED_6X8, "Pump:%s %s", PF_OnOff(s_actuator.pump_on),
                s_actuator.manual_pump_override ? "MAN" : "AUTO");
    OLED_ShowString(0, 54, "BACK:Menu", OLED_6X8);
}

static void PF_Page4GInfo(KeyEvent_t key, uint8_t *exit_flag)
{
    char last_line[22];
    const char *line = AirM2M_4G_GetLastLine();

    PF_ExitOnBack(key, exit_flag);
    if ((exit_flag != NULL) && (*exit_flag != 0U))
    {
        return;
    }

    memset(last_line, 0, sizeof(last_line));
    if ((line != NULL) && (line[0] != '\0'))
    {
        snprintf(last_line, sizeof(last_line), "%s", line);
    }
    else
    {
        snprintf(last_line, sizeof(last_line), "none");
    }

    PF_DrawPageHeader("4G Info");
    OLED_Printf(0, 12, OLED_6X8, "Init:%s 4G:%s", s_onenet_init_ok ? "OK" : "NO",
                AirM2M_4G_GetStatusText());
    OLED_Printf(0, 22, OLED_6X8, "MQTT:%s RDY:%s", OneNet_IsConnected() ? "OK" : "NO",
                OneNet_IsReady() ? "OK" : "NO");
    OLED_Printf(0, 32, OLED_6X8, "UP OK:%lu F:%lu", (unsigned long)s_upload_success_count,
                (unsigned long)s_upload_fail_count);
    OLED_Printf(0, 42, OLED_6X8, "Last:%s", last_line);
    OLED_ShowString(0, 54, "BACK:Menu", OLED_6X8);
}

static void PF_PageUploadRecords(KeyEvent_t key, uint8_t *exit_flag)
{
    uint8_t i;

    PF_ExitOnBack(key, exit_flag);
    if ((exit_flag != NULL) && (*exit_flag != 0U))
    {
        return;
    }

    PF_DrawPageHeader("Upload Log");
    for (i = 0U; i < PETFEEDER_UPLOAD_RECORD_COUNT; i++)
    {
        uint8_t index = (uint8_t)((s_upload_next_index + PETFEEDER_UPLOAD_RECORD_COUNT - 1U - i) %
                                  PETFEEDER_UPLOAD_RECORD_COUNT);
        const PetFeederUploadRecord_t *record = &s_upload_records[index];
        uint8_t y = (uint8_t)(12U + i * 9U);

        if (record->tick_ms == 0U)
        {
            OLED_Printf(0, y, OLED_6X8, "%u --", (unsigned int)(i + 1U));
        }
        else
        {
            uint32_t weight10 = PF_FloatToScaledU32(record->food_weight_g, 10U);
            OLED_Printf(0, y, OLED_6X8, "%u %s %lus T%ld W%lu.%lu", (unsigned int)(i + 1U),
                        record->success ? "OK" : "NO",
                        (unsigned long)(record->tick_ms / 1000U),
                        (long)record->temperature_c,
                        (unsigned long)(weight10 / 10U),
                        (unsigned long)(weight10 % 10U));
        }
    }
}

const PetFeederSensorData_t *PetFeeder_GetSensorData(void)
{
    return &s_sensor;
}

PetFeederThresholds_t *PetFeeder_GetThresholds(void)
{
    return &s_thresholds;
}

const PetFeederActuatorState_t *PetFeeder_GetActuatorState(void)
{
    return &s_actuator;
}

const PetFeederUploadRecord_t *PetFeeder_GetUploadRecords(void)
{
    return s_upload_records;
}

uint8_t PetFeeder_GetUploadRecordNextIndex(void)
{
    return s_upload_next_index;
}

uint32_t PetFeeder_GetUploadSuccessCount(void)
{
    return s_upload_success_count;
}

uint32_t PetFeeder_GetUploadFailCount(void)
{
    return s_upload_fail_count;
}

uint8_t PetFeeder_IsOneNetInitOk(void)
{
    return s_onenet_init_ok;
}
