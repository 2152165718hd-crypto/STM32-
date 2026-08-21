#ifndef __CONFIG_STORAGE_H
#define __CONFIG_STORAGE_H

#include "stm32f1xx_hal.h"

#define CONFIG_DEVICE_ID_MAX 16U
#define CONFIG_FLASH_ADDR    0x0800FC00U
#define CONFIG_FLASH_SIZE    0x00000400U

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t sample_period_s;
    float ph_min;
    float ph_max;
    float temp_min;
    float temp_max;
    float turb_max;
    float ph_k;
    float ph_b;
    float turb_a;
    float turb_b;
    float turb_c;
    char device_id[CONFIG_DEVICE_ID_MAX];
    uint32_t checksum;
} NodeConfig_t;

void ConfigStorage_Load(NodeConfig_t *cfg);
uint8_t ConfigStorage_Save(const NodeConfig_t *cfg);
void ConfigStorage_SetDefaults(NodeConfig_t *cfg);
uint8_t ConfigStorage_IsValid(const NodeConfig_t *cfg);

#endif
