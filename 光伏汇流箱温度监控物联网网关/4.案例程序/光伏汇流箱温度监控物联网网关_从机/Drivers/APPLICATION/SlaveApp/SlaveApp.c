#include ".\APPLICATION\SlaveApp\SlaveApp.h"

#include ".\APPLICATION\EnvProto\EnvProto.h"
#include ".\Hardware\DS18B20\DS18B20.h"
#include ".\Hardware\OLED\OLED.h"
#include ".\Hardware\Zigbee\Zigbee.h"
#include "stm32f1xx_hal.h"

#define SLAVE_NODE_ID              1U
#define SLAVE_REPORT_PERIOD_MS     2000U
#define SLAVE_FRAME_BUF_SIZE       16U

static uint8_t s_seq = 0U;
static uint32_t s_last_report_tick = 0U;
static float s_last_temperature = 0.0f;
static uint8_t s_sensor_online = 0U;

static void SlaveApp_Render(void)
{
    int32_t temp_x10 = (int32_t)(s_last_temperature * 10.0f);

    OLED_Clear();
    OLED_ShowString(28, 0, "Slave", OLED_8X16);
    OLED_Printf(0, 20, OLED_8X16, "Node:%u", (unsigned int)SLAVE_NODE_ID);
    if (s_sensor_online != 0U)
    {
        OLED_Printf(0, 40, OLED_8X16, "T:%ld.%ldC",
                    (long)(temp_x10 / 10L),
                    (long)(temp_x10 % 10L));
    }
    else
    {
        OLED_ShowString(0, 40, "T:ERROR", OLED_8X16);
    }
    OLED_Update();
}

void SlaveApp_Init(void)
{
    s_sensor_online = DS18B20_Init();
    Zigbee_Init();
    s_last_report_tick = HAL_GetTick() - SLAVE_REPORT_PERIOD_MS;
    SlaveApp_Render();
}

void SlaveApp_Task(void)
{
    uint32_t now = HAL_GetTick();
    float temperature;
    int16_t temp_x10 = 0;
    uint8_t status = ENVPROTO_TEMP_STATUS_ERROR;
    uint8_t frame[SLAVE_FRAME_BUF_SIZE];
    uint16_t frame_len;

    Zigbee_Task();

    if ((now - s_last_report_tick) < SLAVE_REPORT_PERIOD_MS)
    {
        return;
    }
    s_last_report_tick = now;

    temperature = DS18B20_ReadTemperature();
    if (temperature != DS18B20_TEMP_ERROR)
    {
        s_last_temperature = temperature;
        temp_x10 = (int16_t)(temperature * 10.0f);
        status = ENVPROTO_TEMP_STATUS_OK;
        s_sensor_online = 1U;
    }
    else
    {
        s_sensor_online = 0U;
    }

    frame_len = EnvProto_BuildTempReport(s_seq++, SLAVE_NODE_ID, temp_x10, status,
                                         frame, (uint16_t)sizeof(frame));
    if (frame_len > 0U)
    {
        (void)Zigbee_SendFrame(frame, frame_len);
    }

    SlaveApp_Render();
}
