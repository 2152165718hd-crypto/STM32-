#include "main.h"
#include <string.h>

#define SLAVE_DHT_INTERVAL_MS      2000U
#define SLAVE_PM25_INTERVAL_MS     1000U
#define SLAVE_MQ135_INTERVAL_MS    500U
#define SLAVE_LIGHT_INTERVAL_MS    1000U
#define SLAVE_REPORT_INTERVAL_MS   1000U
#define SLAVE_DISPLAY_INTERVAL_MS  250U

static SlaveSensorSnapshot_t s_snapshot;

static void Slave_UpdateDht11(void)
{
    if (DHT11_ReadData() == 0U)
    {
        s_snapshot.valid_bits |= ENV_PROTO_VALID_DHT11;
        s_snapshot.temp_c = DHT11_Data.temperature;
        s_snapshot.hum_pct = DHT11_Data.humidity;
    }
    else
    {
        s_snapshot.valid_bits &= (uint8_t)(~ENV_PROTO_VALID_DHT11);
    }
}

static void Slave_UpdatePm25(void)
{
    DC01_PM2_5_Data_t pm25_data;
    uint32_t pm25_value = 0U;

    if (DC01_PM2_5_Read(&pm25_data) != 0U)
    {
        if (pm25_data.calibrated_ug_m3 < 0.0f)
        {
            pm25_value = 0U;
        }
        else
        {
            pm25_value = (uint32_t)(pm25_data.calibrated_ug_m3 + 0.5f);
        }

        if (pm25_value > 65535U)
        {
            pm25_value = 65535U;
        }

        s_snapshot.valid_bits |= ENV_PROTO_VALID_PM25;
        s_snapshot.pm25_ugm3 = (uint16_t)pm25_value;
    }
    else
    {
        s_snapshot.valid_bits &= (uint8_t)(~ENV_PROTO_VALID_PM25);
    }
}

static void Slave_UpdateMq135(void)
{
    float voltage = MQ135_Smoke_GetVoltage();
    uint32_t mv = 0U;

    if (voltage < 0.0f)
    {
        voltage = 0.0f;
    }

    mv = (uint32_t)(voltage * 1000.0f + 0.5f);
    if (mv > 5000U)
    {
        mv = 5000U;
    }

    s_snapshot.valid_bits |= ENV_PROTO_VALID_MQ135;
    s_snapshot.mq135_mv = (uint16_t)mv;
}

static void Slave_UpdateLight(void)
{
    float lux = 0.0f;
    uint32_t light_value = 0U;

    if (BH1750_ReadLux(&lux) == BH1750_OK)
    {
        if (lux < 0.0f)
        {
            lux = 0.0f;
        }

        light_value = (uint32_t)(lux + 0.5f);
        if (light_value > 65535U)
        {
            light_value = 65535U;
        }

        s_snapshot.valid_bits |= ENV_PROTO_VALID_BH1750;
        s_snapshot.light_lux = (uint16_t)light_value;
    }
    else
    {
        s_snapshot.valid_bits &= (uint8_t)(~ENV_PROTO_VALID_BH1750);
    }
}

static void Slave_SendReport(void)
{
    uint8_t frame_buf[20];
    uint8_t frame_len = 0U;

    if (EnvProto_BuildSensorReport(s_snapshot.valid_bits,
                                   s_snapshot.temp_c,
                                   s_snapshot.hum_pct,
                                   s_snapshot.pm25_ugm3,
                                   s_snapshot.mq135_mv,
                                   s_snapshot.light_lux,
                                   frame_buf,
                                   &frame_len) != 0U)
    {
        (void)Zigbee_SendFrame(frame_buf, frame_len);
    }
}

static void Slave_ProcessZigbee(void)
{
    EnvProto_Frame_t frame;

    Zigbee_Task();

    while (Zigbee_GetFrame(&frame) != 0U)
    {
        if (frame.type == ENV_PROTO_TYPE_QUERY_NOW)
        {
            Slave_SendReport();
        }
    }
}

int main(void)
{
    uint32_t last_dht_tick = 0U;
    uint32_t last_pm25_tick = 0U;
    uint32_t last_mq135_tick = 0U;
    uint32_t last_light_tick = 0U;
    uint32_t last_report_tick = 0U;
    uint32_t last_display_tick = 0U;

    HAL_Init();
    sys_stm32_clock_init(RCC_PLL_MUL9);
    delay_init(72);

    OLED_Init();
    DHT11_Init();
    DC01_PM2_5_Init();
    MQ135_Smoke_Init();
    (void)BH1750_Init();
    Zigbee_Init();

    memset(&s_snapshot, 0, sizeof(s_snapshot));
    Menu_ShowSlaveSnapshot(&s_snapshot);

    while (1)
    {
        uint32_t now = HAL_GetTick();

        Slave_ProcessZigbee();

        if ((now - last_dht_tick) >= SLAVE_DHT_INTERVAL_MS)
        {
            last_dht_tick = now;
            Slave_UpdateDht11();
        }

        if ((now - last_pm25_tick) >= SLAVE_PM25_INTERVAL_MS)
        {
            last_pm25_tick = now;
            Slave_UpdatePm25();
        }

        if ((now - last_mq135_tick) >= SLAVE_MQ135_INTERVAL_MS)
        {
            last_mq135_tick = now;
            Slave_UpdateMq135();
        }

        if ((now - last_light_tick) >= SLAVE_LIGHT_INTERVAL_MS)
        {
            last_light_tick = now;
            Slave_UpdateLight();
        }

        if ((now - last_report_tick) >= SLAVE_REPORT_INTERVAL_MS)
        {
            last_report_tick = now;
            Slave_SendReport();
        }

        if ((now - last_display_tick) >= SLAVE_DISPLAY_INTERVAL_MS)
        {
            last_display_tick = now;
            Menu_ShowSlaveSnapshot(&s_snapshot);
        }
    }
}
