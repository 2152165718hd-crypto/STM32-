/**
 * @file Menu.c
 * @brief 智能蓝牙药盒应用层核心业务实现。
 * @note 该文件负责菜单构建、环境采样、阈值保护、服药提醒、日志存储以及蓝牙命令解析。
 */

#include ".\APPLICATION\Menu\Menu.h"
#include ".\APPLICATION\Menu\MenuFramework.h"
#include "main.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ==================== 配置存储与调度参数 ==================== */
#define APP_CFG_MAGIC 0x50424643UL
#define APP_CFG_VERSION 0x00010001UL
#define APP_LOG_MAGIC 0x5042464CUL
#define APP_LOG_VERSION 0x00010001UL

#define APP_FLASH_CFG_ADDR 0x000000UL
#define APP_FLASH_LOG_META_ADDR 0x001000UL
#define APP_FLASH_LOG_BASE 0x002000UL

#define APP_ENV_SAMPLE_MS 1000UL
#define APP_BT_TELEM_MS 3000UL
#define APP_CFG_SAVE_DELAY_MS 1500UL
#define APP_ALERT_BEEP_MS 3000UL
#define APP_REMIND_BLINK_MS 300UL

#define APP_TEMP_HYS 1
#define APP_HUMI_HYS 3
#define APP_LIGHT_HYS 60
#define APP_BATTERY_HYS 2
#define APP_WEIGHT_HYS_MG 1000

#define APP_REMIND_SLOT_COUNT 4
#define APP_BT_CMD_BUF_SIZE 128
#define APP_BT_MAX_TOKENS 16
#define APP_BT_CMD_LOG_COUNT 5
#define APP_BT_CMD_LOG_LEN 48
#define APP_MED_NAME_LEN 16

#define APP_LOG_RECORD_BYTES 128UL
#define APP_LOG_CAPACITY ((W25Q64_TOTAL_SIZE - APP_FLASH_LOG_BASE) / APP_LOG_RECORD_BYTES)
/* 可调的历史最大页数（每页1条记录）；超过后按环形覆盖最早记录。 */
#define APP_LOG_MAX_PAGES 256UL

/* ==================== 事件来源与状态位定义 ==================== */
#define APP_CONFIRM_KEY 1u
#define APP_CONFIRM_BT 2u
#define APP_CONFIRM_MENU 3u

#define APP_STATUS_FLAG_REMIND (1u << 0)
#define APP_STATUS_FLAG_ENV (1u << 1)
#define APP_STATUS_FLAG_LIGHT (1u << 2)
#define APP_STATUS_FLAG_LOW_BAT (1u << 3)
#define APP_STATUS_FLAG_EMPTY (1u << 4)
#define APP_STATUS_FLAG_FAN (1u << 5)
#define APP_STATUS_FLAG_SHADE (1u << 6)

/* ==================== 语音播报词条（GB2312 编码） ==================== */
#define CN_MSG_REMIND "\xC4\xFA\xBA\xC3\xA3\xAC\xCF\xD6\xD4\xDA\xC7\xEB\xB0\xB4\xCA\xB1\xB7\xFE\xD2\xA9"
#define CN_MSG_ENV_ALERT "\xBB\xB7\xBE\xB3\xD2\xEC\xB3\xA3\xA3\xAC\xC7\xEB\xD7\xA2\xD2\xE2"
#define CN_MSG_LIGHT_ALERT "\xB9\xE2\xD5\xD5\xB9\xFD\xC7\xBF\xA3\xAC\xC7\xEB\xD5\xDA\xB9\xE2"
#define CN_MSG_LOW_BAT "\xB5\xE7\xC1\xBF\xB2\xBB\xD7\xE3\xA3\xAC\xC7\xEB\xB3\xE4\xB5\xE7"
#define CN_MSG_EMPTY "\xD2\xA9\xC1\xBF\xB2\xBB\xD7\xE3\xA3\xAC\xC7\xEB\xB2\xB9\xB3\xE4"
#define CN_MSG_RECORDED "\xD2\xD1\xBC\xC7\xC2\xBC\xB7\xFE\xD2\xA9"
#define CN_MSG_STATUS "\xC7\xEB\xB2\xE9\xBF\xB4\xC6\xC1\xC4\xBB\xD7\xB4\xCC\xAC"

#define CN_WORD_TEMP "\xCE\xC2\xB6\xC8"
#define CN_WORD_HUMI "\xCA\xAA\xB6\xC8"
#define CN_WORD_LIGHT "\xB9\xE2\xD5\xD5"
#define CN_WORD_WEIGHT "\xD2\xA9\xB2\xD6\xD6\xD8\xC1\xBF"
#define CN_WORD_BAT "\xB5\xE7\xC1\xBF"
#define CN_PHRASE_NOW "\xB5\xB1\xC7\xB0"
#define CN_PHRASE_EXCEED "\xB3\xAC\xB3\xF6"
#define CN_PHRASE_HIGH "\xC9\xCF\xCF\xDE"
#define CN_PHRASE_LOW "\xCF\xC2\xCF\xDE"
#define CN_PHRASE_THRESHOLD_ALERT "\xE3\xD0\xD6\xB5\xA3\xAC\xC7\xEB\xD7\xA2\xD2\xE2"

#pragma pack(push, 1)
/** @brief 单个服药时段的小时与分钟配置。 */
typedef struct
{
    int32_t hour;
    int32_t minute;
} AppDoseTime_t;

/** @brief 药品名称、单次剂量与每日时段配置。 */
typedef struct
{
    char name[APP_MED_NAME_LEN];
    int32_t dose;
    AppDoseTime_t slots[APP_REMIND_SLOT_COUNT];
} MedConfig_t;

/** @brief 需要持久化保存到 Flash 的应用配置。 */
typedef struct
{
    int32_t temp_min;
    int32_t temp_max;
    int32_t humi_min;
    int32_t humi_max;
    int32_t light_max;
    int32_t battery_low_percent;
    int32_t empty_weight_mg;
    int32_t reserved_weight_cfg;
    int32_t servo_open_angle;
    int32_t servo_close_angle;
    uint8_t voice_enable;
    uint8_t buzzer_enable;
    uint8_t reserved[2];
    MedConfig_t med;
} AppConfig_t;

/** @brief 当前环境采样值与报警标志集合。 */
typedef struct
{
    int32_t temperature;
    int32_t humidity;
    uint16_t light;
    float battery_voltage;
    uint8_t battery_percent;
    float weight_g;
    int32_t weight_mg;
    uint8_t env_alarm;
    uint8_t light_alarm;
    uint8_t low_battery;
    uint8_t empty_box;
} AppEnv_t;

/** @brief 单条服药日志记录。 */
typedef struct
{
    uint8_t year;
    uint8_t month;
    uint8_t date;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t slot_index;
    uint8_t confirm_source;
    int32_t weight_mg;
    int32_t reserved_weight_log;
    uint8_t battery_percent;
    int32_t dose;
    char medicine_name[APP_MED_NAME_LEN];
} AppLogRecord_t;

/** @brief 环形日志区的写指针与计数信息。 */
typedef struct
{
    uint32_t write_index;
    uint32_t count;
    uint32_t next_sequence;
} AppLogMeta_t;

/** @brief 配置区存储镜像，含魔术字与 CRC 校验。 */
typedef struct
{
    uint32_t magic;
    uint32_t version;
    AppConfig_t config;
    uint32_t crc32;
} AppConfigStore_t;

/** @brief 日志元数据存储镜像，含魔术字与 CRC 校验。 */
typedef struct
{
    uint32_t magic;
    uint32_t version;
    AppLogMeta_t meta;
    uint32_t crc32;
} AppLogMetaStore_t;

/** @brief 单页日志存储镜像。 */
typedef struct
{
    uint32_t magic;
    uint32_t sequence;
    AppLogRecord_t record;
    uint32_t crc32;
    uint8_t reserved[APP_LOG_RECORD_BYTES - (sizeof(uint32_t) * 3u) - sizeof(AppLogRecord_t)];
} AppLogStore_t;
#pragma pack(pop)

/* ==================== 运行期全局状态 ==================== */
/* 持久化配置、环境采样与 RTC 当前时间缓存。 */
static AppConfig_t g_config;
static AppEnv_t g_env;
static AppLogMeta_t g_log_meta;
static DS1302_Time_t g_now;

/* 根菜单句柄，由 Menu_Build 在初始化阶段创建。 */
static MF_Menu_t *g_menu_root = NULL;

/* 菜单中“设置时间”页面的编辑缓存，修改后统一回写 RTC。 */
static int32_t g_rtc_year = 26;
static int32_t g_rtc_month = 1;
static int32_t g_rtc_date = 1;
static int32_t g_rtc_hour = 8;
static int32_t g_rtc_minute = 0;
static int32_t g_rtc_second = 0;

/* 提醒状态、采样节拍与配置延时保存状态。 */
static uint8_t g_daily_flags[APP_REMIND_SLOT_COUNT] = {0};
static uint8_t g_last_checked_date = 0;
static uint8_t g_in_remind = 0;
static int8_t g_active_slot = -1;
static uint32_t g_last_env_tick = 0;
static uint8_t g_cfg_dirty = 0;
static uint32_t g_cfg_dirty_tick = 0;
static uint32_t g_alarm_beep_until_tick = 0;
static uint32_t g_remind_blink_tick = 0;
static uint8_t g_remind_blink_on = 0;

/* 执行器的自动/手动控制状态。 */
static uint8_t g_fan_on = 0;
static uint8_t g_fan_auto_required = 0;
static uint8_t g_shade_closed = 0;
static uint8_t g_manual_buzzer_on = 0;
static uint8_t g_manual_fan_on = 0;
static uint8_t g_manual_led_on = 0;

/* 各类环境报警的迟滞锁存标志。 */
static uint8_t g_temp_high_active = 0;
static uint8_t g_temp_low_active = 0;
static uint8_t g_humi_high_active = 0;
static uint8_t g_humi_low_active = 0;
static uint8_t g_light_active = 0;
static uint8_t g_low_battery_active = 0;
static uint8_t g_empty_active = 0;

/* 蓝牙串口命令接收缓存与最近命令日志。 */
static char g_bt_cmd_buf[APP_BT_CMD_BUF_SIZE];
static uint16_t g_bt_cmd_len = 0;
static uint8_t g_bt_data_subscribed = 0u;
static uint32_t g_bt_telemetry_tick = 0u;
static char g_bt_cmd_log[APP_BT_CMD_LOG_COUNT][APP_BT_CMD_LOG_LEN];
static uint8_t g_bt_cmd_log_head = 0u;
static uint8_t g_bt_cmd_log_count = 0u;

/* ==================== 内部函数声明 ==================== */
static uint32_t App_Crc32(const uint8_t *data, uint32_t len);
static void App_CopyString(char *dst, uint32_t dst_size, const char *src);
static uint8_t App_IsGb2312Pair(uint8_t high, uint8_t low);
static void App_FilterGb2312Text(const char *src, char *dst, uint32_t dst_size);
static void App_DefaultConfig(AppConfig_t *config);
static void App_ClampConfig(AppConfig_t *config);
static uint8_t App_IsRtcValid(const DS1302_Time_t *time);
static void App_SetDefaultRtc(DS1302_Time_t *time);
static void App_SyncRtcEdit(const DS1302_Time_t *time);
static void App_LoadConfig(void);
static void App_SaveConfigNow(void);
static void App_MarkConfigDirty(void);
static void App_FlushConfigIfDirty(void);
static void App_LoadLogMeta(void);
static void App_SaveLogMeta(void);
static uint32_t App_LogCapacity(void);
static uint32_t App_LogRecordAddr(uint32_t index);
static void App_UpdateEnv(void);
static void App_StartAlertBeep(void);
static void App_Speak(const char *text);
static uint32_t App_StatusFlags(void);
static void App_SendLineV(const char *prefix, const char *fmt, va_list args);
static void App_SendEvent(const char *fmt, ...);
static void App_SendTelemetry(void);
static void App_ReplyOk(const char *fmt, ...);
static void App_ReplyErr(const char *fmt, ...);
static void App_LogBtCommand(const char *line);
static void App_BuildCmdLogJoined(char *buf, uint32_t buf_size, uint8_t *count_out);
static uint8_t App_SplitCsv(char *text, char **tokens, uint8_t max_tokens);
static uint8_t App_ParseInt32(const char *text, int32_t *value);
static void App_SpeakThresholdAlert(const char *item_cn, uint8_t is_high);
static uint32_t App_SecondsOfDay(const DS1302_Time_t *time);
static uint32_t App_SecondsToSlot(const DS1302_Time_t *time, const AppDoseTime_t *slot);
static int32_t App_MgToGramRound(int32_t mg);
static void App_FormatWeightText(char *buf, uint32_t buf_size, int32_t weight_mg);
static const char *App_OnOffText(uint8_t on);
static uint8_t App_ReadLogByAge(uint32_t age, AppLogRecord_t *record);
static void App_ClearLogHistory(void);
static void On_Config_Value_Changed(int32_t new_value);

/**
 * @brief 计算字节流的 CRC32 校验值。
 * @param data 待计算的数据首地址。
 * @param len 数据长度，单位为字节。
 * @return 计算得到的 CRC32 值。
 * @note 配置区、日志元数据和日志记录都依赖该函数保证掉电校验可靠性。
 */
static uint32_t App_Crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t i;
    uint32_t j;

    for (i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (j = 0; j < 8u; j++)
        {
            if (crc & 1u)
            {
                crc = (crc >> 1) ^ 0xEDB88320UL;
            }
            else
            {
                crc >>= 1;
            }
        }
    }

    return crc ^ 0xFFFFFFFFUL;
}

/**
 * @brief 安全复制字符串并保证目标缓冲区以 `\0` 结尾。
 * @param dst 目标缓冲区。
 * @param dst_size 目标缓冲区大小。
 * @param src 源字符串，可为 `NULL`。
 */
static void App_CopyString(char *dst, uint32_t dst_size, const char *src)
{
    uint32_t i = 0;

    if (dst_size == 0u)
    {
        return;
    }

    if (src == NULL)
    {
        dst[0] = '\0';
        return;
    }

    while ((i + 1u) < dst_size && src[i] != '\0')
    {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/**
 * @brief 判断两个字节是否构成合法的 GB2312 双字节字符。
 * @param high 首字节。
 * @param low 次字节。
 * @return 合法返回 `1`，否则返回 `0`。
 */
static uint8_t App_IsGb2312Pair(uint8_t high, uint8_t low)
{
    if (high < 0xA1u || high > 0xF7u)
        return 0u;

    if (low < 0xA1u || low > 0xFEu)
        return 0u;

    return 1u;
}

/**
 * @brief 过滤字符串中的非法字节，仅保留 ASCII 可打印字符和合法 GB2312 字符。
 * @param src 原始字符串。
 * @param dst 过滤后的输出缓冲区。
 * @param dst_size 输出缓冲区大小。
 * @note 语音芯片仅支持 GB2312，该函数用于避免 UTF-8 文本直接下发导致播报乱码。
 */
static void App_FilterGb2312Text(const char *src, char *dst, uint32_t dst_size)
{
    uint32_t read_idx = 0u;
    uint32_t write_idx = 0u;

    if (dst_size == 0u)
        return;

    dst[0] = '\0';
    if (src == NULL)
        return;

    while (src[read_idx] != '\0' && (write_idx + 1u) < dst_size)
    {
        uint8_t b0 = (uint8_t)src[read_idx];

        if (b0 <= 0x7Fu)
        {
            if (b0 >= 0x20u || b0 == '\t')
                dst[write_idx++] = (char)b0;
            read_idx++;
            continue;
        }

        if (src[read_idx + 1u] == '\0')
            break;

        if ((write_idx + 2u) >= dst_size)
            break;

        if (App_IsGb2312Pair(b0, (uint8_t)src[read_idx + 1u]) != 0u)
        {
            dst[write_idx++] = src[read_idx++];
            dst[write_idx++] = src[read_idx++];
            continue;
        }

        read_idx++;
    }

    dst[write_idx] = '\0';
}

/**
 * @brief 生成一份出厂默认配置。
 * @param config 目标配置对象。
 * @note 当外部 Flash 中没有有效配置时，系统会回落到该默认参数集。
 */
static void App_DefaultConfig(AppConfig_t *config)
{
    memset(config, 0, sizeof(AppConfig_t));

    config->temp_min = 5;
    config->temp_max = 30;
    config->humi_min = 30;
    config->humi_max = 80;
    config->light_max = 2500;
    config->battery_low_percent = 20;
    config->empty_weight_mg = 12000;
    config->reserved_weight_cfg = 0;
    config->servo_open_angle = 0;
    config->servo_close_angle = 90;
    config->voice_enable = 1u;
    config->buzzer_enable = 1u;

    App_CopyString(config->med.name, APP_MED_NAME_LEN, "YAOHE");
    config->med.dose = 1;

    config->med.slots[0].hour = 8;
    config->med.slots[0].minute = 0;
    config->med.slots[1].hour = 12;
    config->med.slots[1].minute = 0;
    config->med.slots[2].hour = 18;
    config->med.slots[2].minute = 0;
    config->med.slots[3].hour = 22;
    config->med.slots[3].minute = 0;
}

/**
 * @brief 对配置项进行限幅修正。
 * @param config 待修正的配置对象。
 * @note 该函数会同时处理上下限关系、布尔开关归一化以及服药时段合法性。
 */
static void App_ClampConfig(AppConfig_t *config)
{
    int i;

    if (config->temp_min < -20)
        config->temp_min = -20;
    if (config->temp_min > 79)
        config->temp_min = 79;
    if (config->temp_max < -19)
        config->temp_max = -19;
    if (config->temp_max > 80)
        config->temp_max = 80;
    if (config->temp_min >= config->temp_max)
    {
        if (config->temp_min >= 79)
        {
            config->temp_min = 79;
            config->temp_max = 80;
        }
        else
        {
            config->temp_max = config->temp_min + 1;
        }
    }

    if (config->humi_min < 0)
        config->humi_min = 0;
    if (config->humi_min > 99)
        config->humi_min = 99;
    if (config->humi_max < 1)
        config->humi_max = 1;
    if (config->humi_max > 100)
        config->humi_max = 100;
    if (config->humi_min >= config->humi_max)
    {
        if (config->humi_min >= 99)
        {
            config->humi_min = 99;
            config->humi_max = 100;
        }
        else
        {
            config->humi_max = config->humi_min + 1;
        }
    }

    if (config->light_max < 0)
        config->light_max = 0;
    if (config->light_max > 4095)
        config->light_max = 4095;

    if (config->battery_low_percent < 1)
        config->battery_low_percent = 1;
    if (config->battery_low_percent > 99)
        config->battery_low_percent = 99;

    if (config->empty_weight_mg < 0)
        config->empty_weight_mg = 0;
    if (config->empty_weight_mg > 200000)
        config->empty_weight_mg = 200000;

    if (config->servo_open_angle < 0)
        config->servo_open_angle = 0;
    if (config->servo_open_angle > 180)
        config->servo_open_angle = 180;
    if (config->servo_close_angle < 0)
        config->servo_close_angle = 0;
    if (config->servo_close_angle > 180)
        config->servo_close_angle = 180;

    config->voice_enable = config->voice_enable ? 1u : 0u;
    config->buzzer_enable = config->buzzer_enable ? 1u : 0u;

    if (config->med.dose < 1)
        config->med.dose = 1;
    if (config->med.dose > 20)
        config->med.dose = 20;

    if (config->med.name[0] == '\0')
        App_CopyString(config->med.name, APP_MED_NAME_LEN, "YAOHE");

    for (i = 0; i < APP_REMIND_SLOT_COUNT; i++)
    {
        if (config->med.slots[i].hour < 0)
            config->med.slots[i].hour = 0;
        if (config->med.slots[i].hour > 23)
            config->med.slots[i].hour = 23;
        if (config->med.slots[i].minute < 0)
            config->med.slots[i].minute = 0;
        if (config->med.slots[i].minute > 59)
            config->med.slots[i].minute = 59;
    }
}

/**
 * @brief 检查 RTC 时间字段是否处于合理范围内。
 * @param time 待检查的时间对象。
 * @return 合法返回 `1`，否则返回 `0`。
 */
static uint8_t App_IsRtcValid(const DS1302_Time_t *time)
{
    if (time->month < 1u || time->month > 12u)
        return 0u;
    if (time->date < 1u || time->date > 31u)
        return 0u;
    if (time->hour > 23u || time->minute > 59u || time->second > 59u)
        return 0u;
    return 1u;
}

/**
 * @brief 写入一组默认 RTC 时间。
 * @param time 待填充的时间对象。
 */
static void App_SetDefaultRtc(DS1302_Time_t *time)
{
    time->year = 26u;
    time->month = 1u;
    time->date = 1u;
    time->day = 1u;
    time->hour = 8u;
    time->minute = 0u;
    time->second = 0u;
}

/**
 * @brief 将当前 RTC 时间同步到菜单编辑缓存。
 * @param time 最新 RTC 时间。
 * @note 菜单中的“年份/月日时分秒”条目直接绑定这些缓存变量。
 */
static void App_SyncRtcEdit(const DS1302_Time_t *time)
{
    g_rtc_year = time->year;
    g_rtc_month = time->month;
    g_rtc_date = time->date;
    g_rtc_hour = time->hour;
    g_rtc_minute = time->minute;
    g_rtc_second = time->second;
}

/**
 * @brief 从外部 Flash 加载应用配置。
 * @note 先校验魔术字、版本号和 CRC，失败时回退到默认配置并立即回写。
 */
static void App_LoadConfig(void)
{
    AppConfigStore_t store;
    uint32_t crc;

    memset(&store, 0, sizeof(store));
    W25Q64_ReadData(APP_FLASH_CFG_ADDR, (uint8_t *)&store, sizeof(store));

    crc = App_Crc32((const uint8_t *)&store.config, sizeof(store.config));
    if (store.magic == APP_CFG_MAGIC &&
        store.version == APP_CFG_VERSION &&
        store.crc32 == crc)
    {
        memcpy(&g_config, &store.config, sizeof(g_config));
        App_ClampConfig(&g_config);
        return;
    }

    App_DefaultConfig(&g_config);
    App_SaveConfigNow();
}

/**
 * @brief 立即将当前配置写入外部 Flash。
 * @note 保存前会重新打包存储镜像并重算 CRC，完成后清除“脏配置”标志。
 */
static void App_SaveConfigNow(void)
{
    AppConfigStore_t store;

    memset(&store, 0, sizeof(store));
    store.magic = APP_CFG_MAGIC;
    store.version = APP_CFG_VERSION;
    memcpy(&store.config, &g_config, sizeof(g_config));
    store.crc32 = App_Crc32((const uint8_t *)&store.config, sizeof(store.config));

    W25Q64_SectorErase(APP_FLASH_CFG_ADDR);
    W25Q64_WriteData(APP_FLASH_CFG_ADDR, (const uint8_t *)&store, sizeof(store));

    g_cfg_dirty = 0u;
}

/**
 * @brief 标记当前配置已被修改。
 * @note 真正的 Flash 写入由 `App_FlushConfigIfDirty` 延时触发，以减少擦写次数。
 */
static void App_MarkConfigDirty(void)
{
    g_cfg_dirty = 1u;
    g_cfg_dirty_tick = HAL_GetTick();
}

/**
 * @brief 在延时窗口到期后落盘保存配置。
 */
static void App_FlushConfigIfDirty(void)
{
    if (g_cfg_dirty == 0u)
        return;

    if ((HAL_GetTick() - g_cfg_dirty_tick) >= APP_CFG_SAVE_DELAY_MS)
        App_SaveConfigNow();
}

/**
 * @brief 加载日志环形缓冲区的元数据。
 * @note 若元数据损坏，则重建一个空日志区状态并从序号 1 开始重新记录。
 */
static void App_LoadLogMeta(void)
{
    AppLogMetaStore_t store;
    uint32_t crc;
    uint32_t cap;

    memset(&store, 0, sizeof(store));
    W25Q64_ReadData(APP_FLASH_LOG_META_ADDR, (uint8_t *)&store, sizeof(store));
    cap = App_LogCapacity();

    crc = App_Crc32((const uint8_t *)&store.meta, sizeof(store.meta));
    if (store.magic == APP_LOG_MAGIC &&
        store.version == APP_LOG_VERSION &&
        store.crc32 == crc &&
        store.meta.write_index < APP_LOG_CAPACITY &&
        store.meta.count <= APP_LOG_CAPACITY)
    {
        g_log_meta = store.meta;
        if (g_log_meta.write_index >= cap)
            g_log_meta.write_index %= cap;
        if (g_log_meta.count > cap)
            g_log_meta.count = cap;
        if (g_log_meta.next_sequence == 0u)
            g_log_meta.next_sequence = 1u;
        return;
    }

    memset(&g_log_meta, 0, sizeof(g_log_meta));
    g_log_meta.next_sequence = 1u;
    App_SaveLogMeta();
}

/**
 * @brief 保存日志元数据到外部 Flash。
 */
static void App_SaveLogMeta(void)
{
    AppLogMetaStore_t store;

    memset(&store, 0, sizeof(store));
    store.magic = APP_LOG_MAGIC;
    store.version = APP_LOG_VERSION;
    store.meta = g_log_meta;
    store.crc32 = App_Crc32((const uint8_t *)&store.meta, sizeof(store.meta));

    W25Q64_SectorErase(APP_FLASH_LOG_META_ADDR);
    W25Q64_WriteData(APP_FLASH_LOG_META_ADDR, (const uint8_t *)&store, sizeof(store));
}

/**
 * @brief 获取日志区实际可用的最大记录条数。
 * @return 环形日志区容量。
 */
static uint32_t App_LogCapacity(void)
{
    return (APP_LOG_CAPACITY < APP_LOG_MAX_PAGES) ? APP_LOG_CAPACITY : APP_LOG_MAX_PAGES;
}

/**
 * @brief 根据日志槽位索引换算外部 Flash 物理地址。
 * @param index 日志槽位索引。
 * @return 对应的 Flash 地址。
 */
static uint32_t App_LogRecordAddr(uint32_t index)
{
    return APP_FLASH_LOG_BASE + index * APP_LOG_RECORD_BYTES;
}

/**
 * @brief 更新当前环境采样缓存。
 * @note 该函数集中刷新温湿度、光照、电池电压、电量和称重值，后续保护逻辑统一基于该缓存运行。
 */
static void App_UpdateEnv(void)
{
    g_env.temperature = (int32_t)DHT11_GetTemperature();
    g_env.humidity = (int32_t)DHT11_GetHumidity();
    g_env.light = LightSensor_ReadAnalog();
    g_env.battery_voltage = Battery_GetVoltage();
    g_env.battery_percent = Battery_GetPercent();
    g_env.weight_g = HX711_GetWeight();
    g_env.weight_mg = (int32_t)(g_env.weight_g * 1000.0f + 0.5f);
    if (g_env.weight_mg < 0)
        g_env.weight_mg = 0;
}

/**
 * @brief 启动一段固定时长的自动报警蜂鸣。
 */
static void App_StartAlertBeep(void)
{
    if (g_config.buzzer_enable)
        g_alarm_beep_until_tick = HAL_GetTick() + APP_ALERT_BEEP_MS;
}

/**
 * @brief 通过语音模块播报指定文本。
 * @param text 待播报文本。
 * @note 函数内部会先做 GB2312 过滤，再根据语音总开关决定是否真正下发。
 */
static void App_Speak(const char *text)
{
    char gb2312_text[192];

    if (g_config.voice_enable && text != NULL && text[0] != '\0')
    {
        /* LU6288 仅支持 GB2312，过滤非法字节以避免 UTF-8 乱码播报。 */
        App_FilterGb2312Text(text, gb2312_text, sizeof(gb2312_text));
        if (gb2312_text[0] != '\0')
            LU6288_Speak(gb2312_text);
    }
}

/**
 * @brief 汇总当前系统状态位。
 * @return 由提醒、环境报警、低电量、缺药、风扇和遮光状态组成的位标志集合。
 */
static uint32_t App_StatusFlags(void)
{
    uint32_t flags = 0u;

    if (g_in_remind)
        flags |= APP_STATUS_FLAG_REMIND;
    if (g_env.env_alarm)
        flags |= APP_STATUS_FLAG_ENV;
    if (g_env.light_alarm)
        flags |= APP_STATUS_FLAG_LIGHT;
    if (g_env.low_battery)
        flags |= APP_STATUS_FLAG_LOW_BAT;
    if (g_env.empty_box)
        flags |= APP_STATUS_FLAG_EMPTY;
    if (g_fan_on)
        flags |= APP_STATUS_FLAG_FAN;
    if (g_shade_closed)
        flags |= APP_STATUS_FLAG_SHADE;

    return flags;
}

/**
 * @brief 按统一协议格式发送一行蓝牙文本消息。
 * @param prefix 协议前缀，例如 `EVT`、`OK`、`ERR`。
 * @param fmt 正文格式串，可为 `NULL`。
 * @param args 可变参数列表。
 */
static void App_SendLineV(const char *prefix, const char *fmt, va_list args)
{
    char body[192];
    char line[224];

    body[0] = '\0';
    if (fmt != NULL && fmt[0] != '\0')
    {
        vsnprintf(body, sizeof(body), fmt, args);
        snprintf(line, sizeof(line), "%s,%s\r\n", prefix, body);
    }
    else
    {
        snprintf(line, sizeof(line), "%s\r\n", prefix);
    }

    Bluetooth_SendString(line);
}

/**
 * @brief 发送事件上报消息。
 * @param fmt 事件正文格式串。
 */
static void App_SendEvent(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    App_SendLineV("EVT", fmt, args);
    va_end(args);
}

/**
 * @brief 发送一帧当前环境遥测数据。
 * @note 数据中包含 RTC 时间、环境值、电量、重量与状态位，供蓝牙上位机订阅显示。
 */
static void App_SendTelemetry(void)
{
    char weight_text[16];

    DS1302_GetTime(&g_now);
    App_FormatWeightText(weight_text, sizeof(weight_text), g_env.weight_mg);
    App_SendEvent("DATA,Time:20%02u-%02u-%02u %02u:%02u:%02u,Temp:%ld,Humi:%ld,Light:%u,Weight:%s,Battery:%u,Flags:%lu",
                  g_now.year,
                  g_now.month,
                  g_now.date,
                  g_now.hour,
                  g_now.minute,
                  g_now.second,
                  (long)g_env.temperature,
                  (long)g_env.humidity,
                  g_env.light,
                  weight_text,
                  g_env.battery_percent,
                  (unsigned long)App_StatusFlags());
}

/**
 * @brief 发送蓝牙命令成功应答。
 * @param fmt 应答正文格式串。
 */
static void App_ReplyOk(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    App_SendLineV("OK", fmt, args);
    va_end(args);
}

/**
 * @brief 发送蓝牙命令失败应答。
 * @param fmt 应答正文格式串。
 */
static void App_ReplyErr(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    App_SendLineV("ERR", fmt, args);
    va_end(args);
}

/**
 * @brief 原地拆分逗号分隔的命令字符串。
 * @param text 待拆分字符串，函数会把逗号替换成 `\0`。
 * @param tokens 输出的字段指针数组。
 * @param max_tokens 最多允许拆分出的字段数。
 * @return 实际拆分得到的字段数。
 */
static uint8_t App_SplitCsv(char *text, char **tokens, uint8_t max_tokens)
{
    uint8_t count = 0u;
    char *cursor = text;

    while (cursor != NULL && *cursor != '\0' && count < max_tokens)
    {
        tokens[count++] = cursor;
        while (*cursor != '\0' && *cursor != ',')
            cursor++;

        if (*cursor == ',')
        {
            *cursor = '\0';
            cursor++;
        }
        else
        {
            break;
        }
    }

    return count;
}

/**
 * @brief 将字符串解析为 32 位整数。
 * @param text 输入文本。
 * @param value 解析成功后的输出值。
 * @return 解析成功返回 `1`，失败返回 `0`。
 */
static uint8_t App_ParseInt32(const char *text, int32_t *value)
{
    char *end_ptr = NULL;
    long v;

    if (text == NULL || *text == '\0')
        return 0u;

    v = strtol(text, &end_ptr, 10);
    if (end_ptr == NULL || *end_ptr != '\0')
        return 0u;

    *value = (int32_t)v;
    return 1u;
}

/**
 * @brief 将最近收到的一条蓝牙命令写入循环日志缓冲区。
 * @param line 原始命令文本。
 * @note 为了适配小屏显示，函数会清理不可打印字符并把逗号改成分号。
 */
static void App_LogBtCommand(const char *line)
{
    uint8_t i = 0u;
    char *dst;

    if (line == NULL || line[0] == '\0')
        return;

    dst = g_bt_cmd_log[g_bt_cmd_log_head];
    while (line[i] != '\0' && i < (APP_BT_CMD_LOG_LEN - 1u))
    {
        char ch = line[i];
        if (ch == ',')
            ch = ';';
        if ((uint8_t)ch < 0x20u || (uint8_t)ch > 0x7Eu)
            ch = ' ';
        dst[i] = ch;
        i++;
    }
    dst[i] = '\0';

    g_bt_cmd_log_head = (uint8_t)((g_bt_cmd_log_head + 1u) % APP_BT_CMD_LOG_COUNT);
    if (g_bt_cmd_log_count < APP_BT_CMD_LOG_COUNT)
        g_bt_cmd_log_count++;
}

/**
 * @brief 将命令日志缓冲区拼接成一行蓝牙返回文本。
 * @param buf 输出缓冲区。
 * @param buf_size 输出缓冲区大小。
 * @param count_out 返回当前日志条数，可为 `NULL`。
 */
static void App_BuildCmdLogJoined(char *buf, uint32_t buf_size, uint8_t *count_out)
{
    uint8_t i;
    uint8_t count = g_bt_cmd_log_count;
    uint32_t used = 0u;

    if (count_out != NULL)
        *count_out = count;

    if (buf_size == 0u)
        return;

    buf[0] = '\0';
    for (i = 0; i < count; i++)
    {
        uint8_t idx = (uint8_t)((g_bt_cmd_log_head + APP_BT_CMD_LOG_COUNT - 1u - i) % APP_BT_CMD_LOG_COUNT);
        int n;

        n = snprintf(&buf[used], buf_size - used, "%s%s", (i == 0u) ? "" : "|", g_bt_cmd_log[idx]);
        if (n < 0)
            break;

        if ((uint32_t)n >= (buf_size - used))
        {
            used = buf_size - 1u;
            break;
        }

        used += (uint32_t)n;
    }
}

/**
 * @brief 在跨天时清空每日提醒触发标志。
 * @param date 当前日期。
 */
static void App_ResetDailyFlags(uint8_t date)
{
    if (g_last_checked_date != date)
    {
        memset(g_daily_flags, 0, sizeof(g_daily_flags));
        g_last_checked_date = date;
    }
}

/**
 * @brief 判断当前时间是否命中某个服药时段。
 * @param time 当前 RTC 时间。
 * @param slot 目标服药时段。
 * @return 命中返回 `1`，否则返回 `0`。
 */
static uint8_t App_TimeSlotDue(const DS1302_Time_t *time, const AppDoseTime_t *slot)
{
    if (time->hour == (uint8_t)slot->hour && time->minute == (uint8_t)slot->minute)
        return 1u;
    return 0u;
}

/**
 * @brief 查找距离当前时间最近的下一个服药时段。
 * @param time 当前 RTC 时间。
 * @param day_offset 返回是否跨天，`0` 表示当天，`1` 表示次日。
 * @return 最近时段下标；若没有有效时段则返回 `-1`。
 */
static int8_t App_GetNextReminderIndex(const DS1302_Time_t *time, uint8_t *day_offset)
{
    int i;
    int now_minutes;
    int best_delta = 0x7FFFFFFF;
    int best_index = -1;

    now_minutes = ((int)time->hour * 60) + (int)time->minute;
    for (i = 0; i < APP_REMIND_SLOT_COUNT; i++)
    {
        int slot_minutes = (int)g_config.med.slots[i].hour * 60 + (int)g_config.med.slots[i].minute;
        int delta = slot_minutes - now_minutes;

        if (delta <= 0)
            delta += 24 * 60;

        if (delta < best_delta)
        {
            best_delta = delta;
            best_index = i;
        }
    }

    if (day_offset != NULL)
    {
        if (best_index >= 0)
            *day_offset = (uint8_t)((now_minutes + best_delta) >= (24 * 60) ? 1u : 0u);
        else
            *day_offset = 0u;
    }

    return (int8_t)best_index;
}

/**
 * @brief 将服药时段格式化为 `HH:MM` 字符串。
 * @param slot 服药时段对象。
 * @param buf 输出缓冲区。
 * @param buf_size 输出缓冲区大小。
 */
static void App_FormatSlot(const AppDoseTime_t *slot, char *buf, uint32_t buf_size)
{
    snprintf(buf, buf_size, "%02ld:%02ld", (long)slot->hour, (long)slot->minute);
}

/**
 * @brief 生成并播报“某指标超上限/下限”的语音提示。
 * @param item_cn 指标中文名称词条。
 * @param is_high 非零表示“超上限”，零表示“低于下限”。
 */
static void App_SpeakThresholdAlert(const char *item_cn, uint8_t is_high)
{
    char text[96];

    snprintf(text,
             sizeof(text),
             CN_PHRASE_NOW "%s" CN_PHRASE_EXCEED "%s" CN_PHRASE_THRESHOLD_ALERT,
             item_cn,
             is_high ? CN_PHRASE_HIGH : CN_PHRASE_LOW);
    App_Speak(text);
}

/**
 * @brief 将当前时间换算为当天已过去的秒数。
 * @param time 时间对象。
 * @return 当天秒数。
 */
static uint32_t App_SecondsOfDay(const DS1302_Time_t *time)
{
    return (uint32_t)time->hour * 3600u + (uint32_t)time->minute * 60u + (uint32_t)time->second;
}

/**
 * @brief 计算当前时间距离指定时段还剩多少秒。
 * @param time 当前 RTC 时间。
 * @param slot 目标时段。
 * @return 剩余秒数；若时段已过则按“明天该时段”计算。
 */
static uint32_t App_SecondsToSlot(const DS1302_Time_t *time, const AppDoseTime_t *slot)
{
    uint32_t now_sec = App_SecondsOfDay(time);
    uint32_t slot_sec = (uint32_t)slot->hour * 3600u + (uint32_t)slot->minute * 60u;

    if (slot_sec <= now_sec)
        slot_sec += 24u * 3600u;
    return slot_sec - now_sec;
}

/**
 * @brief 将毫克值按四舍五入方式转换为克整数。
 * @param mg 毫克值。
 * @return 克整数值。
 */
static int32_t App_MgToGramRound(int32_t mg)
{
    if (mg <= 0)
        return 0;
    return (mg + 500) / 1000;
}

/**
 * @brief 将毫克值格式化为一位小数的克数字符串。
 * @param buf 输出缓冲区。
 * @param buf_size 输出缓冲区大小。
 * @param weight_mg 毫克值。
 */
static void App_FormatWeightText(char *buf, uint32_t buf_size, int32_t weight_mg)
{
    int32_t deci_g;

    if (buf == NULL || buf_size == 0u)
        return;

    if (weight_mg < 0)
        weight_mg = 0;

    deci_g = (weight_mg + 50) / 100;
    snprintf(buf, buf_size, "%ld.%ldg", (long)(deci_g / 10), (long)(deci_g % 10));
}

/**
 * @brief 根据布尔状态返回“开启/关闭”文本。
 * @param on 状态值。
 * @return 中文开关文本。
 */
static const char *App_OnOffText(uint8_t on)
{
    return on ? "开启" : "关闭";
}

/**
 * @brief 生成并播报当前温湿度、电量与药仓重量概况。
 */
static void App_SpeakStatus(void)
{
    char text[160];
    int32_t weight_g = App_MgToGramRound(g_env.weight_mg);

    snprintf(text,
             sizeof(text),
             CN_WORD_TEMP "%ld," CN_WORD_HUMI "%ld," CN_WORD_BAT "%u%%," CN_WORD_WEIGHT "%ldg",
             (long)g_env.temperature,
             (long)g_env.humidity,
             g_env.battery_percent,
             (long)weight_g);

    App_Speak(text);
}

/**
 * @brief 按当前业务状态更新 LED 与蜂鸣器。
 * @note 提醒模式优先级最高，会覆盖普通手动控制与普通报警蜂鸣。
 */
static void App_UpdateIndicators(void)
{
    uint32_t tick = HAL_GetTick();

    if (g_in_remind)
    {
        if ((tick - g_remind_blink_tick) >= APP_REMIND_BLINK_MS)
        {
            g_remind_blink_tick = tick;
            g_remind_blink_on = g_remind_blink_on ? 0u : 1u;
        }

        if (g_remind_blink_on)
        {
            LED_On(MEDICINE_LED_PORT, MEDICINE_LED_PIN);
            if (g_config.buzzer_enable)
                Buzzer_On();
            else
                Buzzer_Off();
        }
        else
        {
            LED_Off(MEDICINE_LED_PORT, MEDICINE_LED_PIN);
            Buzzer_Off();
        }
        return;
    }

    if (g_manual_led_on)
        LED_On(MEDICINE_LED_PORT, MEDICINE_LED_PIN);
    else
        LED_Off(MEDICINE_LED_PORT, MEDICINE_LED_PIN);

    if (g_config.buzzer_enable && tick < g_alarm_beep_until_tick)
        Buzzer_On();
    else if (g_manual_buzzer_on)
        Buzzer_On();
    else
        Buzzer_Off();
}

/**
 * @brief 根据环境阈值执行保护逻辑并联动执行器。
 * @note 该函数会完成迟滞判断、报警上报、风扇联动、遮光舵机联动以及环境状态位更新。
 */
static void App_EnvProtection(void)
{
    uint8_t prev;
    uint8_t fan_should_run;
    int32_t weight_clear;
    char weight_text[16];

    /* 温度过高判断：使用迟滞释放，避免阈值边缘反复抖动。 */
    prev = g_temp_high_active;
    if (g_temp_high_active)
    {
        if (g_env.temperature <= (g_config.temp_max - APP_TEMP_HYS))
            g_temp_high_active = 0u;
    }
    else if (g_env.temperature > g_config.temp_max)
    {
        g_temp_high_active = 1u;
    }

    if (prev == 0u && g_temp_high_active != 0u)
    {
        App_StartAlertBeep();
        App_SpeakThresholdAlert(CN_WORD_TEMP, 1u);
        App_SendEvent("ALARM,TEMP,HIGH,%ld", (long)g_env.temperature);
    }
    else if (prev != 0u && g_temp_high_active == 0u)
    {
        App_SendEvent("ALARM_CLEAR,TEMP,HIGH,%ld", (long)g_env.temperature);
    }

    prev = g_temp_low_active;
    if (g_temp_low_active)
    {
        if (g_env.temperature >= (g_config.temp_min + APP_TEMP_HYS))
            g_temp_low_active = 0u;
    }
    else if (g_env.temperature < g_config.temp_min)
    {
        g_temp_low_active = 1u;
    }

    if (prev == 0u && g_temp_low_active != 0u)
    {
        App_StartAlertBeep();
        App_SpeakThresholdAlert(CN_WORD_TEMP, 0u);
        App_SendEvent("ALARM,TEMP,LOW,%ld", (long)g_env.temperature);
    }
    else if (prev != 0u && g_temp_low_active == 0u)
    {
        App_SendEvent("ALARM_CLEAR,TEMP,LOW,%ld", (long)g_env.temperature);
    }

    prev = g_humi_high_active;
    if (g_humi_high_active)
    {
        if (g_env.humidity <= (g_config.humi_max - APP_HUMI_HYS))
            g_humi_high_active = 0u;
    }
    else if (g_env.humidity > g_config.humi_max)
    {
        g_humi_high_active = 1u;
    }

    if (prev == 0u && g_humi_high_active != 0u)
    {
        App_StartAlertBeep();
        App_SpeakThresholdAlert(CN_WORD_HUMI, 1u);
        App_SendEvent("ALARM,HUMI,HIGH,%ld", (long)g_env.humidity);
    }
    else if (prev != 0u && g_humi_high_active == 0u)
    {
        App_SendEvent("ALARM_CLEAR,HUMI,HIGH,%ld", (long)g_env.humidity);
    }

    prev = g_humi_low_active;
    if (g_humi_low_active)
    {
        if (g_env.humidity >= (g_config.humi_min + APP_HUMI_HYS))
            g_humi_low_active = 0u;
    }
    else if (g_env.humidity < g_config.humi_min)
    {
        g_humi_low_active = 1u;
    }

    if (prev == 0u && g_humi_low_active != 0u)
    {
        App_StartAlertBeep();
        App_SpeakThresholdAlert(CN_WORD_HUMI, 0u);
        App_SendEvent("ALARM,HUMI,LOW,%ld", (long)g_env.humidity);
    }
    else if (prev != 0u && g_humi_low_active == 0u)
    {
        App_SendEvent("ALARM_CLEAR,HUMI,LOW,%ld", (long)g_env.humidity);
    }

    prev = g_light_active;
    if (g_light_active)
    {
        int32_t clear_th = g_config.light_max - APP_LIGHT_HYS;
        if (clear_th < 0)
            clear_th = 0;

        if ((int32_t)g_env.light <= clear_th)
            g_light_active = 0u;
    }
    else if ((int32_t)g_env.light > g_config.light_max)
    {
        g_light_active = 1u;
    }

    if (prev == 0u && g_light_active != 0u)
    {
        App_StartAlertBeep();
        App_SpeakThresholdAlert(CN_WORD_LIGHT, 1u);
        App_SendEvent("ALARM,LIGHT,HIGH,%u", g_env.light);
    }
    else if (prev != 0u && g_light_active == 0u)
    {
        App_SendEvent("ALARM_CLEAR,LIGHT,HIGH,%u", g_env.light);
    }

    prev = g_low_battery_active;
    if (g_low_battery_active)
    {
        if (g_env.battery_percent >= (uint8_t)(g_config.battery_low_percent + APP_BATTERY_HYS))
            g_low_battery_active = 0u;
    }
    else if (g_env.battery_percent < (uint8_t)g_config.battery_low_percent)
    {
        g_low_battery_active = 1u;
    }

    if (prev == 0u && g_low_battery_active != 0u)
    {
        App_StartAlertBeep();
        App_SpeakThresholdAlert(CN_WORD_BAT, 0u);
        App_SendEvent("ALARM,BATTERY,LOW,%u", g_env.battery_percent);
    }
    else if (prev != 0u && g_low_battery_active == 0u)
    {
        App_SendEvent("ALARM_CLEAR,BATTERY,LOW,%u", g_env.battery_percent);
    }

    weight_clear = g_config.empty_weight_mg + APP_WEIGHT_HYS_MG;
    prev = g_empty_active;
    if (g_empty_active)
    {
        if (g_env.weight_mg >= weight_clear)
            g_empty_active = 0u;
    }
    else if (g_env.weight_mg < g_config.empty_weight_mg)
    {
        g_empty_active = 1u;
    }

    if (prev == 0u && g_empty_active != 0u)
    {
        App_StartAlertBeep();
        App_SpeakThresholdAlert(CN_WORD_WEIGHT, 0u);
        App_FormatWeightText(weight_text, sizeof(weight_text), g_env.weight_mg);
        App_SendEvent("ALARM,WEIGHT,LOW,%s", weight_text);
    }
    else if (prev != 0u && g_empty_active == 0u)
    {
        App_FormatWeightText(weight_text, sizeof(weight_text), g_env.weight_mg);
        App_SendEvent("ALARM_CLEAR,WEIGHT,LOW,%s", weight_text);
    }

    /* 风扇由“温高/湿高自动需求”与“手动强制开启”共同决定。 */
    fan_should_run = (uint8_t)(g_temp_high_active || g_humi_high_active);
    g_fan_auto_required = fan_should_run;
    fan_should_run = (uint8_t)(g_fan_auto_required || g_manual_fan_on);
    if (fan_should_run != g_fan_on)
    {
        g_fan_on = fan_should_run;
        if (g_fan_on)
            Motor_Forward();
        else
            Motor_Stop();
    }

    /* 光照过强时闭合遮光机构，恢复正常后重新打开。 */
    if (g_light_active != g_shade_closed)
    {
        g_shade_closed = g_light_active;
        if (g_shade_closed)
            Servo_SetAngle((uint8_t)g_config.servo_close_angle);
        else
            Servo_SetAngle((uint8_t)g_config.servo_open_angle);
    }

    /* 将锁存状态同步回环境缓存，供菜单显示与蓝牙遥测直接读取。 */
    g_env.env_alarm = (uint8_t)(g_temp_high_active || g_temp_low_active || g_humi_high_active || g_humi_low_active);
    g_env.light_alarm = g_light_active;
    g_env.low_battery = g_low_battery_active;
    g_env.empty_box = g_empty_active;
}

/**
 * @brief 追加一条服药日志到外部 Flash。
 * @param source 本次确认来源，见 `APP_CONFIRM_*`。
 * @param slot_index 触发的服药时段下标，若无对应时段则传入负值。
 * @note 日志区按固定记录长度组成环形缓冲，写满后会覆盖最旧记录。
 */
static void App_AppendLog(uint8_t source, int8_t slot_index)
{
    AppLogStore_t store;
    uint32_t addr;
    uint32_t sector_addr;
    uint32_t cap;

    memset(&store, 0, sizeof(store));
    cap = App_LogCapacity();
    if (cap == 0u)
        return;

    DS1302_GetTime(&g_now);
    store.magic = APP_LOG_MAGIC;
    store.sequence = g_log_meta.next_sequence;
    store.record.year = g_now.year;
    store.record.month = g_now.month;
    store.record.date = g_now.date;
    store.record.hour = g_now.hour;
    store.record.minute = g_now.minute;
    store.record.second = g_now.second;
    store.record.slot_index = (slot_index >= 0) ? (uint8_t)slot_index : 0xFFu;
    store.record.confirm_source = source;
    store.record.weight_mg = g_env.weight_mg;
    store.record.reserved_weight_log = 0;
    store.record.battery_percent = g_env.battery_percent;
    store.record.dose = g_config.med.dose;
    App_CopyString(store.record.medicine_name, APP_MED_NAME_LEN, g_config.med.name);
    store.crc32 = App_Crc32((const uint8_t *)&store.record, sizeof(store.record));

    /* 每个扇区开始写入新日志前先擦除，保证后续页编程成功。 */
    addr = App_LogRecordAddr(g_log_meta.write_index);
    sector_addr = addr - (addr % W25Q64_SECTOR_SIZE);
    if ((addr % W25Q64_SECTOR_SIZE) == 0u)
        W25Q64_SectorErase(sector_addr);

    W25Q64_WriteData(addr, (const uint8_t *)&store, sizeof(store));

    g_log_meta.write_index++;
    if (g_log_meta.write_index >= cap)
        g_log_meta.write_index = 0u;

    if (g_log_meta.count < cap)
        g_log_meta.count++;

    g_log_meta.next_sequence++;
    App_SaveLogMeta();
}

/**
 * @brief 读取最新的一条服药日志。
 * @param record 输出日志对象。
 * @return 读取成功返回 `1`，否则返回 `0`。
 */
static uint8_t App_ReadLatestLog(AppLogRecord_t *record)
{
    return App_ReadLogByAge(0u, record);
}

/**
 * @brief 按“距最新记录的偏移量”读取日志。
 * @param age `0` 表示最新，`1` 表示上一条，以此类推。
 * @param record 输出日志对象。
 * @return 读取成功返回 `1`，否则返回 `0`。
 */
static uint8_t App_ReadLogByAge(uint32_t age, AppLogRecord_t *record)
{
    AppLogStore_t store;
    uint32_t cap;
    uint32_t latest_index;
    uint32_t target_index;
    uint32_t crc;

    if (record == NULL)
        return 0u;

    cap = App_LogCapacity();
    if (cap == 0u || g_log_meta.count == 0u || age >= g_log_meta.count)
        return 0u;

    latest_index = (g_log_meta.write_index == 0u) ? (cap - 1u) : (g_log_meta.write_index - 1u);
    target_index = (latest_index + cap - (age % cap)) % cap;

    memset(&store, 0, sizeof(store));
    W25Q64_ReadData(App_LogRecordAddr(target_index), (uint8_t *)&store, sizeof(store));

    crc = App_Crc32((const uint8_t *)&store.record, sizeof(store.record));
    if (store.magic != APP_LOG_MAGIC || store.crc32 != crc)
        return 0u;

    memcpy(record, &store.record, sizeof(*record));
    return 1u;
}

/**
 * @brief 清空服药日志历史。
 * @note 这里只重置元数据，不逐页擦除数据区；旧数据会在后续写入时逐步覆盖。
 */
static void App_ClearLogHistory(void)
{
    g_log_meta.write_index = 0u;
    g_log_meta.count = 0u;
    g_log_meta.next_sequence = 1u;
    App_SaveLogMeta();
    App_SendEvent("LOG,CLEAR,OK");
}

/**
 * @brief 处理一次“用户已服药”的确认动作。
 * @param source 确认来源，可能来自按键、蓝牙或菜单动作。
 * @note 该函数会停止提醒、记录日志并播报“已记录服药”。
 */
static void App_OnTaken(uint8_t source)
{
    int8_t slot = g_active_slot;

    App_UpdateEnv();

    g_in_remind = 0u;
    g_active_slot = -1;
    g_remind_blink_on = 0u;
    g_alarm_beep_until_tick = 0u;
    LED_Off(MEDICINE_LED_PORT, MEDICINE_LED_PIN);
    Buzzer_Off();
    LU6288_Stop();

    App_AppendLog(source, slot);
    App_Speak(CN_MSG_RECORDED);
}

/**
 * @brief 启动指定时段的服药提醒。
 * @param slot_index 时段下标。
 */
static void App_RemindStart(uint8_t slot_index)
{
    char time_text[8];

    if (slot_index >= APP_REMIND_SLOT_COUNT)
        return;

    g_in_remind = 1u;
    g_active_slot = (int8_t)slot_index;
    g_remind_blink_tick = HAL_GetTick();
    g_remind_blink_on = 1u;
    g_alarm_beep_until_tick = 0u;

    App_Speak(CN_MSG_REMIND);
    App_FormatSlot(&g_config.med.slots[slot_index], time_text, sizeof(time_text));
    App_SendEvent("REMIND,%u,%s,%s,%ld",
                  (unsigned int)(slot_index + 1u),
                  time_text,
                  g_config.med.name,
                  (long)g_config.med.dose);
}

/**
 * @brief 绘制服药提醒页面。
 */
static void App_RenderRemindScreen(void)
{
    char line[40];

    OLED_Clear();
    OLED_ShowString(0, 0, "服药提醒", OLED_8X16);
    if (g_active_slot >= 0)
    {
        snprintf(line,
                 sizeof(line),
                 "时段%d %02ld:%02ld",
                 (int)(g_active_slot + 1),
                 (long)g_config.med.slots[g_active_slot].hour,
                 (long)g_config.med.slots[g_active_slot].minute);
        OLED_ShowString(0, 16, line, OLED_8X16);
    }
    OLED_ShowString(0, 32, "药名:", OLED_8X16);
    OLED_ShowString(40, 32, g_config.med.name, OLED_8X16);
    snprintf(line, sizeof(line), "剂量:%ld", (long)g_config.med.dose);
    OLED_ShowString(0, 48, line, OLED_8X16);
    OLED_Update();
}

/**
 * @brief 处理提醒状态下的界面与确认逻辑。
 * @note 在提醒激活期间，菜单界面会被提醒页面接管，按下确认或返回都视作“已服药”。
 */
static void App_RemindProcess(void)
{
    KeyEvent_t key;

    App_UpdateIndicators();
    if (g_in_remind == 0u)
        return;

    key = Key_Scan();
    if (key == KEY_ENTER || key == KEY_BACK)
    {
        App_OnTaken(APP_CONFIRM_KEY);
        return;
    }

    App_RenderRemindScreen();
}

/**
 * @brief 将菜单编辑缓存中的时间值写回 DS1302。
 * @param new_value 菜单框架回调参数，函数内部不直接使用。
 */
static void Save_RTC_Time(int32_t new_value)
{
    DS1302_Time_t time;

    (void)new_value;

    if (g_rtc_year < 0)
        g_rtc_year = 0;
    if (g_rtc_year > 99)
        g_rtc_year = 99;
    if (g_rtc_month < 1)
        g_rtc_month = 1;
    if (g_rtc_month > 12)
        g_rtc_month = 12;
    if (g_rtc_date < 1)
        g_rtc_date = 1;
    if (g_rtc_date > 31)
        g_rtc_date = 31;
    if (g_rtc_hour < 0)
        g_rtc_hour = 0;
    if (g_rtc_hour > 23)
        g_rtc_hour = 23;
    if (g_rtc_minute < 0)
        g_rtc_minute = 0;
    if (g_rtc_minute > 59)
        g_rtc_minute = 59;
    if (g_rtc_second < 0)
        g_rtc_second = 0;
    if (g_rtc_second > 59)
        g_rtc_second = 59;

    time.year = (uint8_t)g_rtc_year;
    time.month = (uint8_t)g_rtc_month;
    time.date = (uint8_t)g_rtc_date;
    time.day = 1u;
    time.hour = (uint8_t)g_rtc_hour;
    time.minute = (uint8_t)g_rtc_minute;
    time.second = (uint8_t)g_rtc_second;
    DS1302_SetTime(&time);
    g_now = time;
    App_ResetDailyFlags(time.date);
}

/**
 * @brief 保存服药时段配置。
 * @param new_value 菜单框架回调参数，函数内部不直接使用。
 */
static void Save_Med_Time(int32_t new_value)
{
    (void)new_value;
    App_ClampConfig(&g_config);
    App_MarkConfigDirty();
}

/**
 * @brief 菜单中阈值类配置变化后的统一处理入口。
 * @param new_value 新值，当前函数仅用于满足回调签名。
 */
static void On_Config_Value_Changed(int32_t new_value)
{
    (void)new_value;
    App_ClampConfig(&g_config);
    App_MarkConfigDirty();
    App_UpdateEnv();
    App_EnvProtection();
}

/**
 * @brief 菜单动作：播报当前状态摘要。
 */
static void Action_Speak(void)
{
    App_SpeakStatus();
}

/**
 * @brief 菜单动作：手动记一次“已服药”。
 */
static void Action_MarkTaken(void)
{
    App_OnTaken(APP_CONFIRM_MENU);
}

/**
 * @brief 光照信息页面。
 * @param key 当前按键事件。
 * @param exit_flag 置位后退出当前自定义页面。
 */
static void Page_SensorLight(KeyEvent_t key, uint8_t *exit_flag)
{
    char line[40];

    if (key == KEY_BACK || key == KEY_ENTER)
    {
        *exit_flag = 1u;
        return;
    }

    OLED_ShowString(0, 0, "光照信息", OLED_8X16);
    snprintf(line, sizeof(line), "当前:%u", g_env.light);
    OLED_ShowString(0, 16, line, OLED_8X16);
    snprintf(line, sizeof(line), "上限:%ld", (long)g_config.light_max);
    OLED_ShowString(0, 32, line, OLED_8X16);
    snprintf(line, sizeof(line), "状态:%s", g_light_active ? "过强" : "正常");
    OLED_ShowString(0, 48, line, OLED_8X16);
}

/**
 * @brief 温湿度信息页面。
 */
static void Page_SensorTempHumi(KeyEvent_t key, uint8_t *exit_flag)
{
    char line[40];

    if (key == KEY_BACK || key == KEY_ENTER)
    {
        *exit_flag = 1u;
        return;
    }

    OLED_ShowString(0, 0, "温湿度", OLED_8X16);
    snprintf(line, sizeof(line), "温度:%ldC %ld~%ld", (long)g_env.temperature, (long)g_config.temp_min, (long)g_config.temp_max);
    OLED_ShowString(0, 16, line, OLED_8X16);
    snprintf(line, sizeof(line), "湿度:%ld%% %ld~%ld", (long)g_env.humidity, (long)g_config.humi_min, (long)g_config.humi_max);
    OLED_ShowString(0, 32, line, OLED_8X16);
    snprintf(line, sizeof(line), "状态:%s", g_env.env_alarm ? "报警" : "正常");
    OLED_ShowString(0, 48, line, OLED_8X16);
}

/**
 * @brief 药仓重量信息页面。
 */
static void Page_SensorWeight(KeyEvent_t key, uint8_t *exit_flag)
{
    char line[40];
    char weight_text[16];
    char min_text[16];

    if (key == KEY_BACK || key == KEY_ENTER)
    {
        *exit_flag = 1u;
        return;
    }

    App_FormatWeightText(weight_text, sizeof(weight_text), g_env.weight_mg);
    App_FormatWeightText(min_text, sizeof(min_text), g_config.empty_weight_mg);

    OLED_ShowString(0, 0, "重量信息", OLED_8X16);
    snprintf(line, sizeof(line), "当前:%s", weight_text);
    OLED_ShowString(0, 16, line, OLED_8X16);
    snprintf(line, sizeof(line), "下限:%s", min_text);
    OLED_ShowString(0, 32, line, OLED_8X16);
    snprintf(line, sizeof(line), "状态:%s", g_env.empty_box ? "偏低" : "正常");
    OLED_ShowString(0, 48, line, OLED_8X16);
}

/**
 * @brief 重量下限调整页面。
 * @note 该页面不是普通数值条目，而是自定义页面，用于同步显示当前重量与下限。
 */
static void Page_ThresholdWeight(KeyEvent_t key, uint8_t *exit_flag)
{
    char line[40];
    char now_text[16];
    char min_text[16];

    if (key == KEY_BACK || key == KEY_ENTER)
    {
        *exit_flag = 1u;
        return;
    }

    if (key == KEY_UP)
    {
        g_config.empty_weight_mg += 100;
        On_Config_Value_Changed(g_config.empty_weight_mg);
    }
    else if (key == KEY_DOWN)
    {
        g_config.empty_weight_mg -= 100;
        On_Config_Value_Changed(g_config.empty_weight_mg);
    }

    App_FormatWeightText(now_text, sizeof(now_text), g_env.weight_mg);
    App_FormatWeightText(min_text, sizeof(min_text), g_config.empty_weight_mg);

    OLED_ShowString(0, 0, "重量下限", OLED_8X16);
    snprintf(line, sizeof(line), "当前:%s", now_text);
    OLED_ShowString(0, 16, line, OLED_8X16);
    snprintf(line, sizeof(line), "下限:%s", min_text);
    OLED_ShowString(0, 32, line, OLED_8X16);
    OLED_ShowString(0, 48, "上下调整确认返回", OLED_8X16);
}

/**
 * @brief 电池信息页面。
 */
static void Page_SensorBattery(KeyEvent_t key, uint8_t *exit_flag)
{
    char line[40];

    if (key == KEY_BACK || key == KEY_ENTER)
    {
        *exit_flag = 1u;
        return;
    }

    OLED_ShowString(0, 0, "电量信息", OLED_8X16);
    snprintf(line, sizeof(line), "电量:%u%%", g_env.battery_percent);
    OLED_ShowString(0, 16, line, OLED_8X16);
    snprintf(line, sizeof(line), "电压:%.2fV", g_env.battery_voltage);
    OLED_ShowString(0, 32, line, OLED_8X16);
    snprintf(line, sizeof(line), "下限:%ld%%", (long)g_config.battery_low_percent);
    OLED_ShowString(0, 48, line, OLED_8X16);
}

/**
 * @brief 当前时间与下次服药预览页面。
 */
static void Page_TimeNow(KeyEvent_t key, uint8_t *exit_flag)
{
    char line[40];
    char next_text[8];
    int8_t next_index;
    uint8_t day_offset = 0u;

    if (key == KEY_BACK || key == KEY_ENTER)
    {
        *exit_flag = 1u;
        return;
    }

    DS1302_GetTime(&g_now);
    next_index = App_GetNextReminderIndex(&g_now, &day_offset);
    if (next_index >= 0)
        App_FormatSlot(&g_config.med.slots[next_index], next_text, sizeof(next_text));
    else
        App_CopyString(next_text, sizeof(next_text), "--:--");

    OLED_ShowString(0, 0, "当前时间", OLED_8X16);
    snprintf(line, sizeof(line), "日期:%02u-%02u-%02u", g_now.year, g_now.month, g_now.date);
    OLED_ShowString(0, 16, line, OLED_8X16);
    snprintf(line, sizeof(line), "时间:%02u:%02u:%02u", g_now.hour, g_now.minute, g_now.second);
    OLED_ShowString(0, 32, line, OLED_8X16);
    snprintf(line, sizeof(line), "下次:%s D+%u", next_text, day_offset);
    OLED_ShowString(0, 48, line, OLED_8X16);
}

/**
 * @brief 下次服药倒计时页面。
 * @note 页面内部会为当前目标时段维护一个总时长基准，用于绘制比例进度条。
 */
static void Page_TimeNextDose(KeyEvent_t key, uint8_t *exit_flag)
{
    static uint8_t s_init = 0u;
    static int8_t s_slot = -1;
    static uint32_t s_total = 1u;
    char line[40];
    char slot_text[8];
    int8_t slot_idx;
    uint8_t day_offset = 0u;
    uint32_t left_sec;
    uint8_t fill_w;

    if (key == KEY_BACK || key == KEY_ENTER)
    {
        s_init = 0u;
        *exit_flag = 1u;
        return;
    }

    DS1302_GetTime(&g_now);
    slot_idx = App_GetNextReminderIndex(&g_now, &day_offset);
    if (slot_idx < 0)
    {
        OLED_ShowString(0, 0, "下次服药", OLED_8X16);
        OLED_ShowString(0, 16, "暂无时段", OLED_8X16);
        return;
    }

    left_sec = App_SecondsToSlot(&g_now, &g_config.med.slots[slot_idx]);
    if (left_sec == 0u)
        left_sec = 1u;

    if ((s_init == 0u) || (s_slot != slot_idx) || (left_sec > s_total))
    {
        s_init = 1u;
        s_slot = slot_idx;
        s_total = left_sec;
        if (s_total == 0u)
            s_total = 1u;
    }

    if (left_sec > s_total)
        left_sec = s_total;

    /* 使用“剩余比例条”并做向上取整，避免长间隔下整数截断导致长期空条。 */
    fill_w = (uint8_t)((left_sec * 118u + s_total - 1u) / s_total);
    App_FormatSlot(&g_config.med.slots[slot_idx], slot_text, sizeof(slot_text));

    OLED_ShowString(0, 0, "下次服药", OLED_8X16);
    snprintf(line, sizeof(line), "时段%d %s D+%u", (int)(slot_idx + 1), slot_text, day_offset);
    OLED_ShowString(0, 16, line, OLED_8X16);
    snprintf(line, sizeof(line), "剩余:%02lu:%02lu:%02lu",
             (unsigned long)(left_sec / 3600u),
             (unsigned long)((left_sec % 3600u) / 60u),
             (unsigned long)(left_sec % 60u));
    OLED_ShowString(0, 32, line, OLED_8X16);
    OLED_DrawRectangle(4, 54, 120, 8, OLED_UNFILLED);
    if (fill_w > 0u)
        OLED_DrawRectangle(5, 55, fill_w, 6, OLED_FILLED);
}

/**
 * @brief 历史日志浏览页面。
 * @note 上键查看更早记录，下键返回较新的记录。
 */
static void Page_LastLog(KeyEvent_t key, uint8_t *exit_flag)
{
    static uint8_t s_opened = 0u;
    static uint32_t s_age = 0u; /* 0=最新，递增表示更早记录 */
    AppLogRecord_t record;
    char line[40];
    char weight_text[16];
    uint32_t total;
    uint32_t page_no;

    if (s_opened == 0u)
    {
        s_opened = 1u;
        s_age = 0u;
    }

    if (key == KEY_BACK || key == KEY_ENTER)
    {
        s_opened = 0u;
        *exit_flag = 1u;
        return;
    }

    total = g_log_meta.count;
    if (total > App_LogCapacity())
        total = App_LogCapacity();
    if (total > 0u && s_age >= total)
        s_age = total - 1u;

    if (key == KEY_UP)
    {
        if ((s_age + 1u) < total)
            s_age++;
    }
    else if (key == KEY_DOWN)
    {
        if (s_age > 0u)
            s_age--;
    }

    OLED_ShowString(0, 0, "历史记录", OLED_8X16);
    if (total == 0u || App_ReadLogByAge(s_age, &record) == 0u)
    {
        OLED_ShowString(0, 16, "暂无记录", OLED_8X16);
        OLED_ShowString(0, 48, "页0/0", OLED_8X16);
        return;
    }

    snprintf(line, sizeof(line), "%02u-%02u %02u:%02u S%u", record.month, record.date, record.hour, record.minute, record.confirm_source);
    OLED_ShowString(0, 16, line, OLED_8X16);
    App_FormatWeightText(weight_text, sizeof(weight_text), record.weight_mg);
    snprintf(line, sizeof(line), "重%s 电%u%%", weight_text, record.battery_percent);
    OLED_ShowString(0, 32, line, OLED_8X16);
    page_no = s_age + 1u;
    snprintf(line, sizeof(line), "剂%ld 页%lu/%lu", (long)record.dose, (unsigned long)page_no, (unsigned long)total);
    OLED_ShowString(0, 48, line, OLED_8X16);
}

/**
 * @brief 清空日志确认页面。
 */
static void Page_DeleteLogs(KeyEvent_t key, uint8_t *exit_flag)
{
    static uint8_t s_opened = 0u;
    static uint8_t s_done = 0u;
    char line[40];
    uint32_t total;

    if (s_opened == 0u)
    {
        s_opened = 1u;
        s_done = 0u;
    }

    if (key == KEY_BACK)
    {
        s_opened = 0u;
        s_done = 0u;
        *exit_flag = 1u;
        return;
    }

    total = g_log_meta.count;
    if (total > App_LogCapacity())
        total = App_LogCapacity();

    if (key == KEY_ENTER)
    {
        App_ClearLogHistory();
        s_done = 1u;
        total = 0u;
    }

    OLED_ShowString(0, 0, "清空记录", OLED_8X16);
    snprintf(line, sizeof(line), "记录:%lu条", (unsigned long)total);
    OLED_ShowString(0, 16, line, OLED_8X16);
    if (s_done)
        OLED_ShowString(0, 32, "已清空", OLED_8X16);
    else
        OLED_ShowString(0, 32, "确定全部清空", OLED_8X16);
    OLED_ShowString(0, 48, "清空后不可恢复", OLED_8X16);
}

/**
 * @brief 蜂鸣器状态与手动控制页面。
 */
static void Page_ActBuzzer(KeyEvent_t key, uint8_t *exit_flag)
{
    char line[40];
    uint8_t auto_on;
    uint8_t actual_on;

    if (key == KEY_BACK)
    {
        *exit_flag = 1u;
        return;
    }
    if (key == KEY_ENTER)
        g_manual_buzzer_on ^= 1u;

    auto_on = (uint8_t)(g_config.buzzer_enable && (HAL_GetTick() < g_alarm_beep_until_tick));
    actual_on = (uint8_t)((g_in_remind && g_remind_blink_on && g_config.buzzer_enable) || auto_on || g_manual_buzzer_on);

    OLED_ShowString(0, 0, "蜂鸣器", OLED_8X16);
    snprintf(line, sizeof(line), "手动:%s", App_OnOffText(g_manual_buzzer_on));
    OLED_ShowString(0, 16, line, OLED_8X16);
    snprintf(line, sizeof(line), "自动:%s", App_OnOffText(auto_on));
    OLED_ShowString(0, 32, line, OLED_8X16);
    snprintf(line, sizeof(line), "实际:%s", App_OnOffText(actual_on));
    OLED_ShowString(0, 48, line, OLED_8X16);
}

/**
 * @brief 风扇状态与手动控制页面。
 */
static void Page_ActFan(KeyEvent_t key, uint8_t *exit_flag)
{
    char line[40];

    if (key == KEY_BACK)
    {
        *exit_flag = 1u;
        return;
    }
    if (key == KEY_ENTER)
    {
        g_manual_fan_on ^= 1u;
        App_EnvProtection();
    }

    OLED_ShowString(0, 0, "风扇", OLED_8X16);
    snprintf(line, sizeof(line), "手动:%s", App_OnOffText(g_manual_fan_on));
    OLED_ShowString(0, 16, line, OLED_8X16);
    snprintf(line, sizeof(line), "自动:%s", App_OnOffText(g_fan_auto_required));
    OLED_ShowString(0, 32, line, OLED_8X16);
    snprintf(line, sizeof(line), "实际:%s", App_OnOffText(g_fan_on));
    OLED_ShowString(0, 48, line, OLED_8X16);
}

/**
 * @brief 指示灯状态与手动控制页面。
 */
static void Page_ActLed(KeyEvent_t key, uint8_t *exit_flag)
{
    char line[40];
    uint8_t actual_on;

    if (key == KEY_BACK)
    {
        *exit_flag = 1u;
        return;
    }
    if (key == KEY_ENTER)
        g_manual_led_on ^= 1u;

    actual_on = (uint8_t)((g_in_remind && g_remind_blink_on) || (!g_in_remind && g_manual_led_on));

    OLED_ShowString(0, 0, "指示灯", OLED_8X16);
    snprintf(line, sizeof(line), "手动:%s", App_OnOffText(g_manual_led_on));
    OLED_ShowString(0, 16, line, OLED_8X16);
    snprintf(line, sizeof(line), "自动:%s", App_OnOffText(g_in_remind));
    OLED_ShowString(0, 32, line, OLED_8X16);
    snprintf(line, sizeof(line), "实际:%s", App_OnOffText(actual_on));
    OLED_ShowString(0, 48, line, OLED_8X16);
}

/**
 * @brief 语音播报开关与试播页面。
 */
static void Page_ActVoice(KeyEvent_t key, uint8_t *exit_flag)
{
    char line[40];

    if (key == KEY_BACK)
    {
        *exit_flag = 1u;
        return;
    }
    if (key == KEY_ENTER)
    {
        g_config.voice_enable = g_config.voice_enable ? 0u : 1u;
        App_MarkConfigDirty();
    }
    if (key == KEY_UP)
        Action_Speak();

    OLED_ShowString(0, 0, "语音播报", OLED_8X16);
    snprintf(line, sizeof(line), "状态:%s", App_OnOffText(g_config.voice_enable));
    OLED_ShowString(0, 16, line, OLED_8X16);
    OLED_ShowString(0, 32, "上键试播", OLED_8X16);
    OLED_ShowString(0, 48, "确定开关", OLED_8X16);
}

/**
 * @brief 最近蓝牙命令日志页面。
 */
static void Page_BtCmdLog(KeyEvent_t key, uint8_t *exit_flag)
{
    char line[28];
    uint8_t i;

    if (key == KEY_BACK || key == KEY_ENTER)
    {
        *exit_flag = 1u;
        return;
    }

    OLED_ShowString(0, 0, "蓝牙日志", OLED_8X16);
    if (g_bt_cmd_log_count == 0u)
    {
        OLED_ShowString(0, 16, "暂无命令", OLED_8X16);
        return;
    }

    for (i = 0u; i < g_bt_cmd_log_count; i++)
    {
        uint8_t idx = (uint8_t)((g_bt_cmd_log_head + APP_BT_CMD_LOG_COUNT - 1u - i) % APP_BT_CMD_LOG_COUNT);
        snprintf(line, sizeof(line), "%u:%-.20s", (unsigned int)(i + 1u), g_bt_cmd_log[idx]);
        OLED_ShowString(0, (int16_t)(18 + i * 8), line, OLED_6X8);
    }
}

/**
 * @brief 解析并执行一整行蓝牙命令。
 * @param line 已经去除换行符的命令文本。
 * @note 当前支持 `GET`、`SET`、`CMD`、`SUB`、`UNSUB` 五类命令，协议字段以逗号分隔。
 */
static void App_ProcessBtLine(char *line)
{
    char *tokens[APP_BT_MAX_TOKENS];
    uint8_t count;
    int32_t i32;
    int32_t idx;
    int32_t hour;
    int32_t minute;
    AppLogRecord_t record;
    char joined[196];
    char weight_text[16];
    uint8_t log_count = 0u;

    count = App_SplitCsv(line, tokens, APP_BT_MAX_TOKENS);
    if (count == 0u)
        return;

    /* GET 类命令：只读查询配置、日志和最近命令缓存。 */
    if (strcmp(tokens[0], "GET") == 0)
    {
        if (count < 2u)
        {
            App_ReplyErr("PARAM");
            return;
        }

        if (strcmp(tokens[1], "CONFIG") == 0)
        {
            App_ReplyOk("CONFIG,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%02ld:%02ld,%02ld:%02ld,%02ld:%02ld,%02ld:%02ld",
                        (long)g_config.light_max,
                        (long)g_config.temp_min,
                        (long)g_config.temp_max,
                        (long)g_config.humi_min,
                        (long)g_config.humi_max,
                        (long)g_config.empty_weight_mg,
                        (long)g_config.battery_low_percent,
                        (long)g_config.med.slots[0].hour, (long)g_config.med.slots[0].minute,
                        (long)g_config.med.slots[1].hour, (long)g_config.med.slots[1].minute,
                        (long)g_config.med.slots[2].hour, (long)g_config.med.slots[2].minute,
                        (long)g_config.med.slots[3].hour, (long)g_config.med.slots[3].minute);
            return;
        }

        if (strcmp(tokens[1], "LASTLOG") == 0)
        {
            if (App_ReadLatestLog(&record) == 0u)
            {
                App_ReplyErr("NOLOG");
                return;
            }

            App_FormatWeightText(weight_text, sizeof(weight_text), record.weight_mg);
            App_ReplyOk("LASTLOG,%02u,%02u,%02u,%02u,%02u,%02u,%u,%u,%s,%u,%s,%ld",
                        record.year,
                        record.month,
                        record.date,
                        record.hour,
                        record.minute,
                        record.second,
                        record.slot_index,
                        record.confirm_source,
                        weight_text,
                        record.battery_percent,
                        record.medicine_name,
                        (long)record.dose);
            return;
        }

        if (strcmp(tokens[1], "CMDLOG") == 0)
        {
            App_BuildCmdLogJoined(joined, sizeof(joined), &log_count);
            if (log_count == 0u)
                App_ReplyOk("CMDLOG,0");
            else
                App_ReplyOk("CMDLOG,%u,%s", (unsigned int)log_count, joined);
            return;
        }

        App_ReplyErr("UNSUPPORTED");
        return;
    }

    /* SET 类命令：写入 RTC、阈值或服药时段配置。 */
    if (strcmp(tokens[0], "SET") == 0)
    {
        if (count < 2u)
        {
            App_ReplyErr("PARAM");
            return;
        }

        if (strcmp(tokens[1], "TIME") == 0)
        {
            if (count != 8u)
            {
                App_ReplyErr("PARAM");
                return;
            }

            if (App_ParseInt32(tokens[2], &g_rtc_year) == 0u ||
                App_ParseInt32(tokens[3], &g_rtc_month) == 0u ||
                App_ParseInt32(tokens[4], &g_rtc_date) == 0u ||
                App_ParseInt32(tokens[5], &g_rtc_hour) == 0u ||
                App_ParseInt32(tokens[6], &g_rtc_minute) == 0u ||
                App_ParseInt32(tokens[7], &g_rtc_second) == 0u)
            {
                App_ReplyErr("FORMAT");
                return;
            }

            Save_RTC_Time(0);
            App_ReplyOk("TIME,SAVED");
            return;
        }

        if (strcmp(tokens[1], "THR") == 0)
        {
            if (count != 9u)
            {
                App_ReplyErr("PARAM");
                return;
            }

            if (App_ParseInt32(tokens[2], &g_config.light_max) == 0u ||
                App_ParseInt32(tokens[3], &g_config.temp_min) == 0u ||
                App_ParseInt32(tokens[4], &g_config.temp_max) == 0u ||
                App_ParseInt32(tokens[5], &g_config.humi_min) == 0u ||
                App_ParseInt32(tokens[6], &g_config.humi_max) == 0u ||
                App_ParseInt32(tokens[7], &g_config.empty_weight_mg) == 0u ||
                App_ParseInt32(tokens[8], &g_config.battery_low_percent) == 0u)
            {
                App_ReplyErr("FORMAT");
                return;
            }

            App_ClampConfig(&g_config);
            App_SaveConfigNow();
            App_UpdateEnv();
            App_EnvProtection();
            App_ReplyOk("THR,SAVED");
            return;
        }

        if (strcmp(tokens[1], "DOSE") == 0)
        {
            if (count != 5u)
            {
                App_ReplyErr("PARAM");
                return;
            }

            if (App_ParseInt32(tokens[2], &idx) == 0u ||
                App_ParseInt32(tokens[3], &hour) == 0u ||
                App_ParseInt32(tokens[4], &minute) == 0u)
            {
                App_ReplyErr("FORMAT");
                return;
            }

            if (idx < 1 || idx > APP_REMIND_SLOT_COUNT || hour < 0 || hour > 23 || minute < 0 || minute > 59)
            {
                App_ReplyErr("RANGE");
                return;
            }

            g_config.med.slots[idx - 1].hour = hour;
            g_config.med.slots[idx - 1].minute = minute;
            App_ClampConfig(&g_config);
            App_SaveConfigNow();
            App_ReplyOk("DOSE,SAVED,%ld,%02ld:%02ld", (long)idx, (long)hour, (long)minute);
            return;
        }

        App_ReplyErr("UNSUPPORTED");
        return;
    }

    /* CMD 类命令：触发即时动作。 */
    if (strcmp(tokens[0], "CMD") == 0)
    {
        if (count < 2u)
        {
            App_ReplyErr("PARAM");
            return;
        }

        if (strcmp(tokens[1], "SPEAK") == 0)
        {
            App_SpeakStatus();
            App_ReplyOk("SPEAK");
            return;
        }

        if (strcmp(tokens[1], "TAKEN") == 0)
        {
            App_OnTaken(APP_CONFIRM_BT);
            App_ReplyOk("TAKEN");
            return;
        }

        App_ReplyErr("UNSUPPORTED");
        return;
    }

    /* SUB/UNSUB：控制是否周期推送环境遥测。 */
    if (strcmp(tokens[0], "SUB") == 0)
    {
        if (count == 2u && strcmp(tokens[1], "DATA") == 0)
        {
            g_bt_data_subscribed = 1u;
            g_bt_telemetry_tick = HAL_GetTick();
            App_ReplyOk("SUB,DATA");
            App_SendTelemetry();
            return;
        }
        App_ReplyErr("UNSUPPORTED");
        return;
    }

    if (strcmp(tokens[0], "UNSUB") == 0)
    {
        if (count == 2u && strcmp(tokens[1], "DATA") == 0)
        {
            g_bt_data_subscribed = 0u;
            App_ReplyOk("UNSUB,DATA");
            return;
        }
        App_ReplyErr("UNSUPPORTED");
        return;
    }

    if (App_ParseInt32(tokens[0], &i32) != 0u)
    {
        App_ReplyErr("UNSUPPORTED");
        return;
    }

    App_ReplyErr("UNKNOWN");
}

/**
 * @brief 从蓝牙串口接收缓冲区取出字节并组装命令行。
 * @note 以 `\n` 作为命令结束符，超长命令会被丢弃并返回 `ERR,TOOLONG`。
 */
static void App_ProcessBluetooth(void)
{
    uint8_t byte;

    while (Bluetooth_ReadByte(&byte))
    {
        if (byte == '\r')
            continue;

        if (byte == '\n')
        {
            if (g_bt_cmd_len > 0u)
            {
                g_bt_cmd_buf[g_bt_cmd_len] = '\0';
                App_LogBtCommand(g_bt_cmd_buf);
                App_ProcessBtLine(g_bt_cmd_buf);
                g_bt_cmd_len = 0u;
            }
            continue;
        }

        if (g_bt_cmd_len < (APP_BT_CMD_BUF_SIZE - 1u))
        {
            g_bt_cmd_buf[g_bt_cmd_len++] = (char)byte;
        }
        else
        {
            g_bt_cmd_len = 0u;
            App_ReplyErr("TOOLONG");
        }
    }
}

/**
 * @brief 构建整棵菜单树。
 * @note 根菜单下划分为传感数据、阈值设置、时间管理、记录管理、设备控制与蓝牙日志六类入口。
 */
static void Menu_Build(void)
{
    MF_Menu_t *menu_sensor;
    MF_Menu_t *menu_thr;
    MF_Menu_t *menu_thr_temp_humi;
    MF_Menu_t *menu_time;
    MF_Menu_t *menu_time_set;
    MF_Menu_t *menu_time_dose;
    MF_Menu_t *menu_storage;
    MF_Menu_t *menu_actuator;

    /* 先创建所有菜单页对象，再按层级关系逐步挂接条目。 */
    g_menu_root = MF_CreateMenu("主菜单");
    menu_sensor = MF_CreateMenu("传感数据");
    menu_thr = MF_CreateMenu("阈值设置");
    menu_thr_temp_humi = MF_CreateMenu("温湿阈值");
    menu_time = MF_CreateMenu("时间管理");
    menu_time_set = MF_CreateMenu("设置时间");
    menu_time_dose = MF_CreateMenu("服药时段");
    menu_storage = MF_CreateMenu("记录管理");
    menu_actuator = MF_CreateMenu("设备控制");

    /* 根菜单。 */
    MF_AddSubmenu(g_menu_root, "传感数据", menu_sensor);
    MF_AddSubmenu(g_menu_root, "阈值设置", menu_thr);
    MF_AddSubmenu(g_menu_root, "时间管理", menu_time);
    MF_AddSubmenu(g_menu_root, "记录管理", menu_storage);
    MF_AddSubmenu(g_menu_root, "设备控制", menu_actuator);
    MF_AddCustomPage(g_menu_root, "蓝牙日志", Page_BtCmdLog);

    /* 传感数据页。 */
    MF_AddCustomPage(menu_sensor, "光照", Page_SensorLight);
    MF_AddCustomPage(menu_sensor, "温湿度", Page_SensorTempHumi);
    MF_AddCustomPage(menu_sensor, "重量", Page_SensorWeight);
    MF_AddCustomPage(menu_sensor, "电量", Page_SensorBattery);

    /* 阈值设置页。 */
    MF_AddValue(menu_thr, "光照上限", &g_config.light_max, 0, 4095, 10, "", On_Config_Value_Changed);
    MF_AddSubmenu(menu_thr, "温湿阈值", menu_thr_temp_humi);
    MF_AddCustomPage(menu_thr, "重量下限", Page_ThresholdWeight);
    MF_AddValue(menu_thr, "电量下限", &g_config.battery_low_percent, 1, 99, 1, "%", On_Config_Value_Changed);

    /* 温湿度阈值子页。 */
    MF_AddValue(menu_thr_temp_humi, "温度下限", &g_config.temp_min, -20, 79, 1, "C", On_Config_Value_Changed);
    MF_AddValue(menu_thr_temp_humi, "温度上限", &g_config.temp_max, -19, 80, 1, "C", On_Config_Value_Changed);
    MF_AddValue(menu_thr_temp_humi, "湿度下限", &g_config.humi_min, 0, 99, 1, "%", On_Config_Value_Changed);
    MF_AddValue(menu_thr_temp_humi, "湿度·········································上限", &g_config.humi_max, 1, 100, 1, "%", On_Config_Value_Changed);

    /* 时间管理页。 */
    MF_AddCustomPage(menu_time, "当前时间", Page_TimeNow);
    MF_AddSubmenu(menu_time, "设置时间", menu_time_set);
    MF_AddSubmenu(menu_time, "服药时段", menu_time_dose);
    MF_AddCustomPage(menu_time, "下次服药", Page_TimeNextDose);

    /* RTC 设置子页。 */
    MF_AddValue(menu_time_set, "年份", &g_rtc_year, 0, 99, 1, "", Save_RTC_Time);
    MF_AddValue(menu_time_set, "月份", &g_rtc_month, 1, 12, 1, "", Save_RTC_Time);
    MF_AddValue(menu_time_set, "日期", &g_rtc_date, 1, 31, 1, "", Save_RTC_Time);
    MF_AddValue(menu_time_set, "小时", &g_rtc_hour, 0, 23, 1, "", Save_RTC_Time);
    MF_AddValue(menu_time_set, "分钟", &g_rtc_minute, 0, 59, 1, "", Save_RTC_Time);
    MF_AddValue(menu_time_set, "秒钟", &g_rtc_second, 0, 59, 1, "", Save_RTC_Time);

    /* 每日四个服药时段配置页。 */
    MF_AddValue(menu_time_dose, "1时", &g_config.med.slots[0].hour, 0, 23, 1, "", Save_Med_Time);
    MF_AddValue(menu_time_dose, "1分", &g_config.med.slots[0].minute, 0, 59, 1, "", Save_Med_Time);
    MF_AddValue(menu_time_dose, "2时", &g_config.med.slots[1].hour, 0, 23, 1, "", Save_Med_Time);
    MF_AddValue(menu_time_dose, "2分", &g_config.med.slots[1].minute, 0, 59, 1, "", Save_Med_Time);
    MF_AddValue(menu_time_dose, "3时", &g_config.med.slots[2].hour, 0, 23, 1, "", Save_Med_Time);
    MF_AddValue(menu_time_dose, "3分", &g_config.med.slots[2].minute, 0, 59, 1, "", Save_Med_Time);
    MF_AddValue(menu_time_dose, "4时", &g_config.med.slots[3].hour, 0, 23, 1, "", Save_Med_Time);
    MF_AddValue(menu_time_dose, "4分", &g_config.med.slots[3].minute, 0, 59, 1, "", Save_Med_Time);

    /* 日志管理页与执行器控制页。 */
    MF_AddAction(menu_storage, "记录服药", Action_MarkTaken);
    MF_AddCustomPage(menu_storage, "历史记录", Page_LastLog);
    MF_AddCustomPage(menu_storage, "清空记录", Page_DeleteLogs);

    MF_AddCustomPage(menu_actuator, "蜂鸣器", Page_ActBuzzer);
    MF_AddCustomPage(menu_actuator, "风扇", Page_ActFan);
    MF_AddCustomPage(menu_actuator, "指示灯", Page_ActLed);
    MF_AddCustomPage(menu_actuator, "语音播报", Page_ActVoice);
}

/**
 * @brief 初始化应用层所有外设、业务状态与菜单树。
 */
void Menu_Init(void)
{
    /* 初始化顺序遵循“基础外设 -> 业务存储 -> 运行态复位 -> 菜单启动”。 */
    OLED_Init();
    Key_Init();
    DHT11_Init();
    HX711_Init();
    HX711_Tare(12);
    ADC_Light_Battery_Init();
    DS1302_Init();
    Motor_Init();
    Servo_Init();
    LED_Init();
    Buzzer_Init();
    LU6288_Init();
    Bluetooth_Init();
    W25Q64_Init();

    App_LoadConfig();
    App_LoadLogMeta();

    /* 若 RTC 数据异常，则回写一组默认时间，避免后续提醒逻辑失效。 */
    DS1302_GetTime(&g_now);
    if (App_IsRtcValid(&g_now) == 0u)
    {
        App_SetDefaultRtc(&g_now);
        DS1302_SetTime(&g_now);
    }

    App_SyncRtcEdit(&g_now);
    App_ResetDailyFlags(g_now.date);

    g_in_remind = 0u;
    g_active_slot = -1;
    g_alarm_beep_until_tick = 0u;
    g_remind_blink_on = 0u;
    g_manual_buzzer_on = 0u;
    g_manual_fan_on = 0u;
    g_manual_led_on = 0u;
    g_fan_auto_required = 0u;
    g_bt_data_subscribed = 0u;
    g_bt_telemetry_tick = HAL_GetTick();
    g_bt_cmd_log_head = 0u;
    g_bt_cmd_log_count = 0u;
    memset(g_bt_cmd_log, 0, sizeof(g_bt_cmd_log));

    /* 将执行器恢复到上电默认安全状态。 */
    Motor_Stop();
    Servo_SetAngle((uint8_t)g_config.servo_open_angle);
    LED_Off(MEDICINE_LED_PORT, MEDICINE_LED_PIN);
    Buzzer_Off();

    App_UpdateEnv();
    App_EnvProtection();

    /* 最后构建菜单并启动框架。 */
    Menu_Build();
    MF_SetCursorStyle(MF_CURSOR_BOX);
    MF_Start(g_menu_root);
    g_last_env_tick = HAL_GetTick();

    App_SendEvent("BOOT,OK");
}

/**
 * @brief 周期执行应用层任务调度。
 * @note 主循环中持续调用即可。函数内部按固定节拍完成采样、报警、提醒、蓝牙与菜单刷新。
 */
void Menu_Task(void)
{
    uint32_t tick = HAL_GetTick();
    int i;

    /* 周期采样环境，并在整秒级节拍上判断是否到达提醒时段。 */
    if ((tick - g_last_env_tick) >= APP_ENV_SAMPLE_MS)
    {
        g_last_env_tick = tick;

        App_UpdateEnv();
        App_EnvProtection();
        DS1302_GetTime(&g_now);
        App_ResetDailyFlags(g_now.date);

        if (g_in_remind == 0u)
        {
            for (i = 0; i < APP_REMIND_SLOT_COUNT; i++)
            {
                if (g_daily_flags[i] == 0u && App_TimeSlotDue(&g_now, &g_config.med.slots[i]) != 0u)
                {
                    /* 每个时段每天只触发一次，触发后立即进入提醒界面。 */
                    g_daily_flags[i] = 1u;
                    App_RemindStart((uint8_t)i);
                    break;
                }
            }
        }
    }

    /* 提醒态优先于菜单态，语音与蓝牙处理则在两种状态下都持续运行。 */
    App_RemindProcess();
    LU6288_Process();
    App_ProcessBluetooth();
    if (g_bt_data_subscribed && (tick - g_bt_telemetry_tick) >= APP_BT_TELEM_MS)
    {
        g_bt_telemetry_tick = tick;
        App_SendTelemetry();
    }
    App_FlushConfigIfDirty();

    if (g_in_remind == 0u)
    {
        App_UpdateIndicators();
        MF_Loop();
    }
}


