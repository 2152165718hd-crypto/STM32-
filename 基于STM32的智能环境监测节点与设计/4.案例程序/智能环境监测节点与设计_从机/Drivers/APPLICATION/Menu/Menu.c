#include ".\Application\Menu\Menu.h"
#include ".\APPLICATION\EnvProto\EnvProto.h"
#include ".\Hardware\OLED\OLED.h"

static uint8_t Slave_GetMq135Percent(uint16_t mq135_mv)
{
    uint32_t percent = ((uint32_t)mq135_mv * 100U + 2500U) / 5000U;

    if (percent > 100U)
    {
        percent = 100U;
    }

    return (uint8_t)percent;
}

void Menu_ShowSlaveSnapshot(const SlaveSensorSnapshot_t *snapshot)
{
    if (snapshot == NULL)
    {
        return;
    }

    OLED_Clear();

    if ((snapshot->valid_bits & ENV_PROTO_VALID_DHT11) != 0U)
    {
        OLED_Printf(0, 0, OLED_6X8, "T:%uC H:%u%%",
                    (unsigned int)snapshot->temp_c,
                    (unsigned int)snapshot->hum_pct);
    }
    else
    {
        OLED_Printf(0, 0, OLED_6X8, "T:-- H:--");
    }

    if ((snapshot->valid_bits & ENV_PROTO_VALID_PM25) != 0U)
    {
        OLED_Printf(0, 16, OLED_6X8, "PM:%u ug/m3", (unsigned int)snapshot->pm25_ugm3);
    }
    else
    {
        OLED_Printf(0, 16, OLED_6X8, "PM:--");
    }

    if ((snapshot->valid_bits & ENV_PROTO_VALID_MQ135) != 0U)
    {
        OLED_Printf(0, 32, OLED_6X8, "MQ:%u%% %umV",
                    (unsigned int)Slave_GetMq135Percent(snapshot->mq135_mv),
                    (unsigned int)snapshot->mq135_mv);
    }
    else
    {
        OLED_Printf(0, 32, OLED_6X8, "MQ:--");
    }

    if ((snapshot->valid_bits & ENV_PROTO_VALID_BH1750) != 0U)
    {
        OLED_Printf(0, 48, OLED_6X8, "L:%u lx", (unsigned int)snapshot->light_lux);
    }
    else
    {
        OLED_Printf(0, 48, OLED_6X8, "L:--");
    }

    OLED_Update();
}
