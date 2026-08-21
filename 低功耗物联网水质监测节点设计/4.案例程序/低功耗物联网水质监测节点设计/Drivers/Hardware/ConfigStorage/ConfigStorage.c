#include ".\Hardware\ConfigStorage\ConfigStorage.h"
#include <stddef.h>
#include <string.h>

#define CONFIG_MAGIC   0x57514E44U
#define CONFIG_VERSION 1U

static uint32_t ConfigStorage_Checksum(const NodeConfig_t *cfg)
{
    const uint8_t *bytes = (const uint8_t *)cfg;
    uint32_t hash = 2166136261UL;
    uint32_t len = (uint32_t)offsetof(NodeConfig_t, checksum);

    for (uint32_t i = 0U; i < len; i++)
    {
        hash ^= bytes[i];
        hash *= 16777619UL;
    }

    return hash;
}

static uint8_t ConfigStorage_IsLegacyDefault(const NodeConfig_t *cfg)
{
    if (cfg == NULL)
    {
        return 0U;
    }

    return ((cfg->sample_period_s == 60U) &&
            (cfg->ph_min == 6.5f) &&
            (cfg->ph_max == 8.5f) &&
            (cfg->temp_min == 0.0f) &&
            (cfg->temp_max == 40.0f) &&
            (cfg->turb_max == 100.0f) &&
            (cfg->ph_k == -5.70f) &&
            (cfg->ph_b == 21.34f) &&
            (cfg->turb_a == -1120.4f) &&
            (cfg->turb_b == 5742.3f) &&
            (cfg->turb_c == -4352.9f) &&
            (memcmp(cfg->device_id, "WQNODE01", 9U) == 0)) ? 1U : 0U;
}

void ConfigStorage_SetDefaults(NodeConfig_t *cfg)
{
    if (cfg == NULL)
    {
        return;
    }

    memset(cfg, 0, sizeof(NodeConfig_t));
    cfg->magic = CONFIG_MAGIC;
    cfg->version = CONFIG_VERSION;
    cfg->size = sizeof(NodeConfig_t);
    cfg->sample_period_s = 20U;
    cfg->ph_min = 6.5f;
    cfg->ph_max = 8.5f;
    cfg->temp_min = 0.0f;
    cfg->temp_max = 40.0f;
    cfg->turb_max = 100.0f;
    cfg->ph_k = -5.70f;
    cfg->ph_b = 21.34f;
    cfg->turb_a = -1120.4f;
    cfg->turb_b = 5742.3f;
    cfg->turb_c = -4352.9f;
    memcpy(cfg->device_id, "WQNODE01", 9U);
    cfg->checksum = ConfigStorage_Checksum(cfg);
}

uint8_t ConfigStorage_IsValid(const NodeConfig_t *cfg)
{
    if (cfg == NULL)
    {
        return 0U;
    }

    if ((cfg->magic != CONFIG_MAGIC) ||
        (cfg->version != CONFIG_VERSION) ||
        (cfg->size != sizeof(NodeConfig_t)) ||
        (cfg->checksum != ConfigStorage_Checksum(cfg)))
    {
        return 0U;
    }

    if ((cfg->sample_period_s < 5U) || (cfg->sample_period_s > 86400U))
    {
        return 0U;
    }

    if ((cfg->ph_min >= cfg->ph_max) || (cfg->turb_max < 0.0f))
    {
        return 0U;
    }

    return 1U;
}

void ConfigStorage_Load(NodeConfig_t *cfg)
{
    const NodeConfig_t *stored = (const NodeConfig_t *)CONFIG_FLASH_ADDR;

    if (cfg == NULL)
    {
        return;
    }

    memcpy(cfg, stored, sizeof(NodeConfig_t));
    if (ConfigStorage_IsValid(cfg) == 0U)
    {
        ConfigStorage_SetDefaults(cfg);
    }
    else if (ConfigStorage_IsLegacyDefault(cfg) != 0U)
    {
        cfg->sample_period_s = 20U;
        cfg->checksum = ConfigStorage_Checksum(cfg);
        (void)ConfigStorage_Save(cfg);
    }
}

uint8_t ConfigStorage_Save(const NodeConfig_t *cfg)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t page_error = 0U;
    NodeConfig_t local;
    const uint32_t *words;
    uint32_t word_count;
    HAL_StatusTypeDef status;

    if (cfg == NULL)
    {
        return 0U;
    }

    memcpy(&local, cfg, sizeof(NodeConfig_t));
    local.magic = CONFIG_MAGIC;
    local.version = CONFIG_VERSION;
    local.size = sizeof(NodeConfig_t);
    local.device_id[CONFIG_DEVICE_ID_MAX - 1U] = '\0';
    local.checksum = ConfigStorage_Checksum(&local);

    if (ConfigStorage_IsValid(&local) == 0U)
    {
        return 0U;
    }

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = CONFIG_FLASH_ADDR;
    erase.NbPages = 1U;

    HAL_FLASH_Unlock();
    status = HAL_FLASHEx_Erase(&erase, &page_error);
    if (status == HAL_OK)
    {
        words = (const uint32_t *)&local;
        word_count = sizeof(NodeConfig_t) / sizeof(uint32_t);
        for (uint32_t i = 0U; i < word_count; i++)
        {
            status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                                       CONFIG_FLASH_ADDR + (i * sizeof(uint32_t)),
                                       words[i]);
            if (status != HAL_OK)
            {
                break;
            }
        }
    }
    HAL_FLASH_Lock();

    return (status == HAL_OK) ? 1U : 0U;
}
