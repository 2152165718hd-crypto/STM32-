#ifndef __MENU_H__
#define __MENU_H__

#include "stm32f1xx_hal.h"

typedef struct
{
    uint8_t valid_bits;
    uint8_t temp_c;
    uint8_t hum_pct;
    uint16_t pm25_ugm3;
    uint16_t mq135_mv;
    uint16_t light_lux;
} SlaveSensorSnapshot_t;

void Menu_ShowSlaveSnapshot(const SlaveSensorSnapshot_t *snapshot);

#endif /* __MENU_H__ */
