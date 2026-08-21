#include ".\APPLICATION\WaterNode\WaterNode.h"
#include ".\Hardware\BoardADC\BoardADC.h"
#include ".\Hardware\ConfigStorage\ConfigStorage.h"
#include ".\Hardware\DS18B20\DS18B20.h"
#include ".\Hardware\KEY\KEY.h"
#include ".\Hardware\LED\LED.h"
#include ".\Hardware\Lora_L33_433UD22S\Lora_L33_433UD22S.h"
#include ".\Hardware\LowPower\LowPower.h"
#include ".\Hardware\OLED\OLED.h"
#include ".\Hardware\PH\PH.h"
#include ".\Hardware\TURBIDTY\TURBIDTY.h"
#include ".\SYSTEM\delay\delay.h"
#include ".\SYSTEM\sys\sys.h"

#include <stdlib.h>
#include <string.h>

#define WATER_SLEEP_DEFAULT_S 20U
#define WATER_WAKE_DEFAULT_S   10U
#define WATER_ACTIVE_WINDOW_MS (WATER_WAKE_DEFAULT_S * 1000U)
#define WATER_WAKE_SPLASH_MS   1000U
#define WATER_UI_REFRESH_MS    50U
#define WATER_DS18B20_WAIT_MS 750U
#define WATER_LED_BLINK_MS 500U
#define WATER_TX_BUF_SIZE 384U
#define WATER_CMD_MAX 24U
#if (WATER_DEBUG_MODE == 1)
#define WATER_DEBUG_PERIOD_MS 1000U
#endif

#define WATER_ERR_TEMP 0x01U
#define WATER_ERR_ADC 0x02U
#define WATER_ERR_LORA 0x04U
#define WATER_ERR_LOW_POWER 0x08U

typedef struct
{
    float temp_c;
    PH_Data_t ph;
    Turbidity_Data_t turb;
    uint8_t alarm;
    uint8_t err;
} WaterSample_t;

typedef enum
{
    WATER_UI_WAKE = 0U,
    WATER_UI_REALTIME,
    WATER_UI_THRESHOLD
} WaterUiMode_t;

static NodeConfig_t s_cfg;
static uint32_t s_seq = 0U;
static uint32_t s_sleep_accum_s = 0U;
static uint8_t s_sample_requested = 1U;
static uint8_t s_last_alarm = 0U;
static uint8_t s_lora_ready = 0U;
static uint8_t s_init_err = 0U;
static uint32_t s_last_blink_tick = 0U;
static uint8_t s_alarm_led_on = 0U; /* 1 = 亮，0 = 灭；LED 低电平点亮 */
static WaterUiMode_t s_ui_mode = WATER_UI_WAKE;
static uint8_t s_ui_dirty = 1U;
static uint8_t s_last_rendered_mode = 0xFFU;
static uint8_t s_last_rendered_wake = 0xFFU;
static uint32_t s_wake_banner_end_tick = 0U;
static WaterSample_t s_last_sample;
static uint8_t s_last_sample_valid = 0U;
static char s_tx_buf[WATER_TX_BUF_SIZE];

#if (WATER_PROTOCOL == WATER_PROTOCOL_CN)
#define CN_CMD_SET_SAMPLE   "设置周期"
#define CN_CMD_SET_THRESHOLD "设置阈值"
#define CN_CMD_SET_CAL      "设置校准"
#define CN_CMD_GET_CONFIG   "获取配置"
#define CN_CMD_SAMPLE_NOW   "立即采样"
#define CN_CMD_PING         "心跳"
#define CN_CMD_UNKNOWN      "未知"

#define CN_KEY_PERIOD  "周期"
#define CN_KEY_PH      "PH"
#define CN_KEY_TEMP    "温度"
#define CN_KEY_TURB    "浊度"
#define CN_KEY_PHK     "PHK"
#define CN_KEY_PHB     "PHB"
#define CN_KEY_TURB_A  "浊度A"
#define CN_KEY_TURB_B  "浊度B"
#define CN_KEY_TURB_C  "浊度C"
#endif

static char *AppendChar(char *p, char *end, char ch)
{
    if (p < (end - 1))
    {
        *p++ = ch;
        *p = '\0';
    }
    return p;
}

static char *AppendText(char *p, char *end, const char *text)
{
    while ((text != NULL) && (*text != '\0') && (p < (end - 1)))
    {
        *p++ = *text++;
    }
    *p = '\0';
    return p;
}

static char *AppendU32(char *p, char *end, uint32_t value)
{
    char tmp[10];
    uint8_t idx = 0U;

    if (value == 0U)
    {
        return AppendChar(p, end, '0');
    }

    while ((value > 0U) && (idx < sizeof(tmp)))
    {
        tmp[idx++] = (char)('0' + (value % 10U));
        value /= 10U;
    }

    while (idx > 0U)
    {
        p = AppendChar(p, end, tmp[--idx]);
    }

    return p;
}

static char *AppendFixed(char *p, char *end, float value, uint8_t frac_digits)
{
    uint32_t scale = 1U;
    uint32_t scaled;
    uint32_t int_part;
    uint32_t frac_part;

    for (uint8_t i = 0U; i < frac_digits; i++)
    {
        scale *= 10U;
    }

    if (value < 0.0f)
    {
        p = AppendChar(p, end, '-');
        value = -value;
    }

    scaled = (uint32_t)((value * (float)scale) + 0.5f);
    int_part = scaled / scale;
    frac_part = scaled % scale;

    p = AppendU32(p, end, int_part);
    if (frac_digits > 0U)
    {
        uint32_t div = scale / 10U;

        p = AppendChar(p, end, '.');
        while (div > 0U)
        {
            p = AppendChar(p, end, (char)('0' + ((frac_part / div) % 10U)));
            div /= 10U;
        }
    }

    return p;
}

static const char *SkipSpaces(const char *p)
{
    while ((*p == ' ') || (*p == '\t') || (*p == '\r') || (*p == '\n'))
    {
        p++;
    }

    return p;
}

#if (WATER_PROTOCOL == WATER_PROTOCOL_JSON)
static const char *FindJsonKey(const char *json, const char *key)
{
    char pattern[32];
    uint8_t i = 0U;

    if ((json == NULL) || (key == NULL))
    {
        return NULL;
    }

    pattern[i++] = '"';
    while ((*key != '\0') && (i < (sizeof(pattern) - 2U)))
    {
        pattern[i++] = *key++;
    }
    pattern[i++] = '"';
    pattern[i] = '\0';

    return strstr(json, pattern);
}

static uint8_t JsonGetCmd(const char *json, char *cmd, uint8_t max_len)
{
    const char *p = FindJsonKey(json, "cmd");
    uint8_t len = 0U;

    if ((p == NULL) || (cmd == NULL) || (max_len == 0U))
    {
        return 0U;
    }

    p = strchr(p, ':');
    if (p == NULL)
    {
        return 0U;
    }

    p = SkipSpaces(p + 1);
    if (*p != '"')
    {
        return 0U;
    }
    p++;

    while ((*p != '\0') && (*p != '"') && (len < (max_len - 1U)))
    {
        cmd[len++] = *p++;
    }
    cmd[len] = '\0';

    return (*p == '"') ? 1U : 0U;
}

static uint8_t JsonGetU32(const char *json, const char *key, uint32_t *value)
{
    const char *p = FindJsonKey(json, key);
    char *endptr;
    unsigned long parsed;

    if ((p == NULL) || (value == NULL))
    {
        return 0U;
    }

    p = strchr(p, ':');
    if (p == NULL)
    {
        return 0U;
    }

    p = SkipSpaces(p + 1);
    parsed = strtoul(p, &endptr, 10);
    if (endptr == p)
    {
        return 0U;
    }

    *value = (uint32_t)parsed;
    return 1U;
}

static uint8_t JsonGetFloat(const char *json, const char *key, float *value)
{
    const char *p = FindJsonKey(json, key);
    char *endptr;
    double parsed;

    if ((p == NULL) || (value == NULL))
    {
        return 0U;
    }

    p = strchr(p, ':');
    if (p == NULL)
    {
        return 0U;
    }

    p = SkipSpaces(p + 1);
    parsed = strtod(p, &endptr);
    if (endptr == p)
    {
        return 0U;
    }

    *value = (float)parsed;
    return 1U;
}
#else
static uint8_t CnGetCmd(const char *line, char *cmd, uint8_t max_len)
{
    uint8_t len = 0U;

    if ((line == NULL) || (cmd == NULL) || (max_len == 0U))
    {
        return 0U;
    }

    line = SkipSpaces(line);
    if (*line == '\0')
    {
        return 0U;
    }

    while ((*line != '\0') && (*line != ' ') && (*line != '\t') && (len < (max_len - 1U)))
    {
        cmd[len++] = *line++;
    }
    cmd[len] = '\0';

    return (len > 0U) ? 1U : 0U;
}

static const char *FindKeyValue(const char *line, const char *key)
{
    size_t key_len;
    const char *p;

    if ((line == NULL) || (key == NULL))
    {
        return NULL;
    }

    key_len = strlen(key);
    p = line;
    while ((p = strstr(p, key)) != NULL)
    {
        const char *q = p + key_len;

        q = SkipSpaces(q);
        if (*q == '=')
        {
            return SkipSpaces(q + 1);
        }

        p = p + key_len;
    }

    return NULL;
}

static uint8_t CnGetU32ByKey(const char *line, const char *key, uint32_t *value)
{
    const char *p = FindKeyValue(line, key);
    char *endptr;
    unsigned long parsed;

    if ((p == NULL) || (value == NULL))
    {
        return 0U;
    }

    parsed = strtoul(p, &endptr, 10);
    if (endptr == p)
    {
        return 0U;
    }

    *value = (uint32_t)parsed;
    return 1U;
}

static uint8_t CnGetFloatByKey(const char *line, const char *key, float *value)
{
    const char *p = FindKeyValue(line, key);
    char *endptr;
    double parsed;

    if ((p == NULL) || (value == NULL))
    {
        return 0U;
    }

    parsed = strtod(p, &endptr);
    if (endptr == p)
    {
        return 0U;
    }

    *value = (float)parsed;
    return 1U;
}

static uint8_t CnGetRangeFloat(const char *line, const char *key, float *min, float *max)
{
    const char *p = FindKeyValue(line, key);
    char *endptr;
    double left;
    double right;

    if ((p == NULL) || (min == NULL) || (max == NULL))
    {
        return 0U;
    }

    left = strtod(p, &endptr);
    if (endptr == p)
    {
        return 0U;
    }

    p = SkipSpaces(endptr);
    if (*p != '~')
    {
        return 0U;
    }
    p = SkipSpaces(p + 1);

    right = strtod(p, &endptr);
    if (endptr == p)
    {
        return 0U;
    }

    *min = (float)left;
    *max = (float)right;
    return 1U;
}

static uint8_t CnGetU32AfterCmd(const char *line, uint32_t *value)
{
    const char *p = SkipSpaces(line);
    char *endptr;
    unsigned long parsed;

    if ((line == NULL) || (value == NULL))
    {
        return 0U;
    }

    while ((*p != '\0') && (*p != ' ') && (*p != '\t'))
    {
        p++;
    }
    p = SkipSpaces(p);
    if (*p == '\0')
    {
        return 0U;
    }

    parsed = strtoul(p, &endptr, 10);
    if (endptr == p)
    {
        return 0U;
    }

    *value = (uint32_t)parsed;
    return 1U;
}
#endif

static uint8_t IsFiniteRange(float value, float min, float max)
{
    return ((value >= min) && (value <= max)) ? 1U : 0U;
}

static void SendAck(const char *cmd, uint8_t ok, const char *err);

static uint8_t SaveConfigWithAck(const char *cmd)
{
    if (ConfigStorage_Save(&s_cfg) == 0U)
    {
        SendAck(cmd, 0U, "flash");
        return 0U;
    }

    return 1U;
}

#if (WATER_PROTOCOL == WATER_PROTOCOL_JSON)
static void SendAck(const char *cmd, uint8_t ok, const char *err)
{
    char *p = s_tx_buf;
    char *end = s_tx_buf + sizeof(s_tx_buf);

    p = AppendText(p, end, "{\"type\":\"ack\",\"cmd\":\"");
    p = AppendText(p, end, cmd);
    p = AppendText(p, end, "\",\"ok\":");
    p = AppendChar(p, end, ok != 0U ? '1' : '0');
    if ((ok == 0U) && (err != NULL))
    {
        p = AppendText(p, end, ",\"err\":\"");
        p = AppendText(p, end, err);
        p = AppendChar(p, end, '"');
    }
    p = AppendText(p, end, "}\r\n");

    if (s_lora_ready != 0U)
    {
        (void)Lora_SendString(s_tx_buf);
    }
}

static void SendConfig(void)
{
    char *p = s_tx_buf;
    char *end = s_tx_buf + sizeof(s_tx_buf);

    p = AppendText(p, end, "{\"type\":\"ack\",\"cmd\":\"get_config\",\"ok\":1,\"id\":\"");
    p = AppendText(p, end, s_cfg.device_id);
    p = AppendText(p, end, "\",\"period_s\":");
    p = AppendU32(p, end, s_cfg.sample_period_s);
    p = AppendText(p, end, ",\"ph_min\":");
    p = AppendFixed(p, end, s_cfg.ph_min, 2U);
    p = AppendText(p, end, ",\"ph_max\":");
    p = AppendFixed(p, end, s_cfg.ph_max, 2U);
    p = AppendText(p, end, ",\"temp_min\":");
    p = AppendFixed(p, end, s_cfg.temp_min, 1U);
    p = AppendText(p, end, ",\"temp_max\":");
    p = AppendFixed(p, end, s_cfg.temp_max, 1U);
    p = AppendText(p, end, ",\"turb_max\":");
    p = AppendFixed(p, end, s_cfg.turb_max, 1U);
    p = AppendText(p, end, ",\"ph_k\":");
    p = AppendFixed(p, end, s_cfg.ph_k, 2U);
    p = AppendText(p, end, ",\"ph_b\":");
    p = AppendFixed(p, end, s_cfg.ph_b, 2U);
    p = AppendText(p, end, ",\"turb_a\":");
    p = AppendFixed(p, end, s_cfg.turb_a, 1U);
    p = AppendText(p, end, ",\"turb_b\":");
    p = AppendFixed(p, end, s_cfg.turb_b, 1U);
    p = AppendText(p, end, ",\"turb_c\":");
    p = AppendFixed(p, end, s_cfg.turb_c, 1U);
    p = AppendText(p, end, "}\r\n");

    if (s_lora_ready != 0U)
    {
        (void)Lora_SendString(s_tx_buf);
    }
}

static void SendTelemetry(const WaterSample_t *sample)
{
    char *p = s_tx_buf;
    char *end = s_tx_buf + sizeof(s_tx_buf);
    uint32_t uptime_s = s_sleep_accum_s + (HAL_GetTick() / 1000U);

    p = AppendText(p, end, "{\"type\":\"telemetry\",\"id\":\"");
    p = AppendText(p, end, s_cfg.device_id);
    p = AppendText(p, end, "\",\"seq\":");
    p = AppendU32(p, end, s_seq);
    p = AppendText(p, end, ",\"uptime_s\":");
    p = AppendU32(p, end, uptime_s);
    p = AppendText(p, end, ",\"temp_c\":");
    p = AppendFixed(p, end, sample->temp_c, 1U);
    p = AppendText(p, end, ",\"ph\":");
    p = AppendFixed(p, end, sample->ph.ph, 2U);
    p = AppendText(p, end, ",\"turb_ntu\":");
    p = AppendFixed(p, end, sample->turb.ntu, 1U);
    p = AppendText(p, end, ",\"alarm\":");
    p = AppendChar(p, end, sample->alarm != 0U ? '1' : '0');
    p = AppendText(p, end, ",\"err\":");
    p = AppendU32(p, end, sample->err);
    p = AppendText(p, end, "}\r\n");

    if (s_lora_ready != 0U)
    {
        (void)Lora_SendString(s_tx_buf);
    }
}
#else
static void SendAck(const char *cmd, uint8_t ok, const char *err)
{
    char *p = s_tx_buf;
    char *end = s_tx_buf + sizeof(s_tx_buf);

    p = AppendText(p, end, "应答 ");
    p = AppendText(p, end, (cmd != NULL) ? cmd : CN_CMD_UNKNOWN);
    p = AppendChar(p, end, ' ');
    p = AppendText(p, end, ok != 0U ? "OK" : "ERR");
    if ((ok == 0U) && (err != NULL))
    {
        p = AppendText(p, end, " 错误=");
        p = AppendText(p, end, err);
    }
    p = AppendText(p, end, "\r\n");

    if (s_lora_ready != 0U)
    {
        (void)Lora_SendString(s_tx_buf);
    }
}

static void SendConfig(void)
{
    char *p = s_tx_buf;
    char *end = s_tx_buf + sizeof(s_tx_buf);

    p = AppendText(p, end, "应答 ");
    p = AppendText(p, end, CN_CMD_GET_CONFIG);
    p = AppendText(p, end, " OK ID=");
    p = AppendText(p, end, s_cfg.device_id);
    p = AppendText(p, end, " 周期=");
    p = AppendU32(p, end, s_cfg.sample_period_s);
    p = AppendText(p, end, " PH=");
    p = AppendFixed(p, end, s_cfg.ph_min, 2U);
    p = AppendChar(p, end, '~');
    p = AppendFixed(p, end, s_cfg.ph_max, 2U);
    p = AppendText(p, end, " 温度=");
    p = AppendFixed(p, end, s_cfg.temp_min, 1U);
    p = AppendChar(p, end, '~');
    p = AppendFixed(p, end, s_cfg.temp_max, 1U);
    p = AppendText(p, end, " 浊度=");
    p = AppendFixed(p, end, s_cfg.turb_max, 1U);
    p = AppendText(p, end, " PHK=");
    p = AppendFixed(p, end, s_cfg.ph_k, 2U);
    p = AppendText(p, end, " PHB=");
    p = AppendFixed(p, end, s_cfg.ph_b, 2U);
    p = AppendText(p, end, " 浊度A=");
    p = AppendFixed(p, end, s_cfg.turb_a, 1U);
    p = AppendText(p, end, " 浊度B=");
    p = AppendFixed(p, end, s_cfg.turb_b, 1U);
    p = AppendText(p, end, " 浊度C=");
    p = AppendFixed(p, end, s_cfg.turb_c, 1U);
    p = AppendText(p, end, "\r\n");

    if (s_lora_ready != 0U)
    {
        (void)Lora_SendString(s_tx_buf);
    }
}

static void SendTelemetry(const WaterSample_t *sample)
{
    char *p = s_tx_buf;
    char *end = s_tx_buf + sizeof(s_tx_buf);
    uint32_t uptime_s = s_sleep_accum_s + (HAL_GetTick() / 1000U);

    p = AppendText(p, end, "数据 ID=");
    p = AppendText(p, end, s_cfg.device_id);
    p = AppendText(p, end, " 序号=");
    p = AppendU32(p, end, s_seq);
    p = AppendText(p, end, " 运行=");
    p = AppendU32(p, end, uptime_s);
    p = AppendText(p, end, " 温度=");
    p = AppendFixed(p, end, sample->temp_c, 1U);
    p = AppendText(p, end, " PH=");
    p = AppendFixed(p, end, sample->ph.ph, 2U);
    p = AppendText(p, end, " 浊度=");
    p = AppendFixed(p, end, sample->turb.ntu, 1U);
    p = AppendText(p, end, " 报警=");
    p = AppendChar(p, end, sample->alarm != 0U ? '1' : '0');
    p = AppendText(p, end, " 错误=");
    p = AppendU32(p, end, sample->err);
    p = AppendText(p, end, "\r\n");

    if (s_lora_ready != 0U)
    {
        (void)Lora_SendString(s_tx_buf);
    }
}
#endif

static void DrawWakeScreen(const WaterSample_t *sample)
{
    OLED_Clear();
    OLED_ShowString(0, 0, "Wake Up", OLED_6X8);
    OLED_ShowString(0, 16, "Sleep:", OLED_6X8);
    OLED_ShowNum(42, 16, s_cfg.sample_period_s, 5U, OLED_6X8);
    OLED_ShowString(78, 16, "s", OLED_6X8);
    OLED_ShowString(0, 32, "Wake:", OLED_6X8);
    OLED_ShowNum(42, 32, WATER_WAKE_DEFAULT_S, 5U, OLED_6X8);
    OLED_ShowString(78, 32, "s", OLED_6X8);
    OLED_ShowString(0, 48, "UP/DN View", OLED_6X8);
    if (sample != NULL)
    {
        OLED_ShowString(0, 56, (sample->alarm != 0U) ? "ALM" : "OK", OLED_6X8);
    }
    OLED_Update();
}

static void DrawRealtimeScreen(const WaterSample_t *sample)
{
    OLED_Clear();
    OLED_ShowString(0, 0, "Real Data", OLED_6X8);

    OLED_ShowString(0, 8, "Temp:", OLED_6X8);
    if ((sample->err & WATER_ERR_TEMP) != 0U)
    {
        OLED_ShowString(36, 8, "ERR", OLED_6X8);
    }
    else
    {
        OLED_ShowFloatNum(36, 8, sample->temp_c, 2U, 1U, OLED_6X8);
        OLED_ShowString(84, 8, "C", OLED_6X8);
    }

    OLED_ShowString(0, 16, "pH:", OLED_6X8);
    OLED_ShowFloatNum(24, 16, sample->ph.ph, 2U, 2U, OLED_6X8);

    OLED_ShowString(0, 24, "Turb:", OLED_6X8);
    OLED_ShowFloatNum(36, 24, sample->turb.ntu, 4U, 1U, OLED_6X8);

    OLED_ShowString(0, 32, "Alarm:", OLED_6X8);
    OLED_ShowNum(42, 32, sample->alarm, 1U, OLED_6X8);

    OLED_ShowString(0, 40, "Seq:", OLED_6X8);
    OLED_ShowNum(30, 40, s_seq, 5U, OLED_6X8);

    OLED_ShowString(0, 48, "Sleep:", OLED_6X8);
    OLED_ShowNum(42, 48, s_cfg.sample_period_s, 5U, OLED_6X8);
    OLED_ShowString(78, 48, "s", OLED_6X8);

    OLED_ShowString(0, 56, "UP/DN Thr", OLED_6X8);
    OLED_Update();
}

static void DrawThresholdScreen(void)
{
    OLED_Clear();
    OLED_ShowString(0, 0, "Threshold", OLED_6X8);

    OLED_ShowString(0, 8, "pH L:", OLED_6X8);
    OLED_ShowFloatNum(36, 8, s_cfg.ph_min, 1U, 2U, OLED_6X8);

    OLED_ShowString(0, 16, "pH H:", OLED_6X8);
    OLED_ShowFloatNum(36, 16, s_cfg.ph_max, 1U, 2U, OLED_6X8);

    OLED_ShowString(0, 24, "Tmp L:", OLED_6X8);
    OLED_ShowFloatNum(36, 24, s_cfg.temp_min, 2U, 1U, OLED_6X8);

    OLED_ShowString(0, 32, "Tmp H:", OLED_6X8);
    OLED_ShowFloatNum(36, 32, s_cfg.temp_max, 2U, 1U, OLED_6X8);

    OLED_ShowString(0, 40, "Turb<", OLED_6X8);
    OLED_ShowFloatNum(36, 40, s_cfg.turb_max, 4U, 1U, OLED_6X8);

    OLED_ShowString(0, 48, "Sleep:", OLED_6X8);
    OLED_ShowNum(42, 48, s_cfg.sample_period_s, 5U, OLED_6X8);
    OLED_ShowString(78, 48, "s", OLED_6X8);

    OLED_ShowString(0, 56, "UP/DN Data", OLED_6X8);
    OLED_Update();
}

static void DrawSleep(uint32_t seconds)
{
    OLED_Clear();
    OLED_ShowString(0, 0, "WaterNode", OLED_6X8);
    OLED_ShowString(0, 16, "Sleep", OLED_6X8);
    OLED_ShowNum(42, 16, seconds, 5U, OLED_6X8);
    OLED_ShowString(78, 16, "s", OLED_6X8);
    OLED_ShowString(0, 32, "Wake", OLED_6X8);
    OLED_ShowNum(42, 32, WATER_WAKE_DEFAULT_S, 5U, OLED_6X8);
    OLED_ShowString(78, 32, "s", OLED_6X8);
    OLED_Update();
}

static uint8_t CalcAlarm(const WaterSample_t *sample)
{
    if ((sample->err & WATER_ERR_TEMP) == 0U)
    {
        if ((sample->temp_c < s_cfg.temp_min) || (sample->temp_c > s_cfg.temp_max))
        {
            return 1U;
        }
    }

    if ((sample->ph.ph < s_cfg.ph_min) || (sample->ph.ph > s_cfg.ph_max))
    {
        return 1U;
    }

    if (sample->turb.ntu > s_cfg.turb_max)
    {
        return 1U;
    }

    if (sample->ph.digital_alarm != 0U)
    {
        return 1U;
    }

    return 0U;
}

static void UpdateAlarmLed(void)
{
    uint32_t now = HAL_GetTick();

    if (s_last_alarm == 0U)
    {
        s_alarm_led_on = 0U;
        LED_Off();
        return;
    }

    if ((now - s_last_blink_tick) >= WATER_LED_BLINK_MS)
    {
        s_last_blink_tick = now;
        s_alarm_led_on ^= 1U;
    }

    /* 低电平点亮，避免依赖 Toggle 造成状态歧义。 */
    if (s_alarm_led_on != 0U)
    {
        LED_On();
    }
    else
    {
        LED_Off();
    }
}

static void UpdateUiState(void)
{
    if ((s_ui_mode == WATER_UI_WAKE) && (HAL_GetTick() >= s_wake_banner_end_tick))
    {
        s_ui_mode = WATER_UI_REALTIME;
        s_ui_dirty = 1U;
        s_last_rendered_wake = 0U;
    }
}

static void RenderUi(void)
{
    if (s_last_sample_valid == 0U)
    {
        return;
    }

    UpdateUiState();

    if (s_ui_mode == WATER_UI_WAKE)
    {
        if ((s_ui_dirty == 0U) && (s_last_rendered_mode == (uint8_t)WATER_UI_WAKE) && (s_last_rendered_wake != 0U))
        {
            return;
        }

        s_last_rendered_mode = (uint8_t)WATER_UI_WAKE;
        s_last_rendered_wake = 1U;
        s_ui_dirty = 0U;
        DrawWakeScreen(&s_last_sample);
        return;
    }

    if ((s_ui_dirty == 0U) && (s_last_rendered_mode == (uint8_t)s_ui_mode))
    {
        return;
    }

    s_last_rendered_mode = (uint8_t)s_ui_mode;
    s_last_rendered_wake = 0U;
    s_ui_dirty = 0U;

    if (s_ui_mode == WATER_UI_THRESHOLD)
    {
        DrawThresholdScreen();
    }
    else
    {
        DrawRealtimeScreen(&s_last_sample);
    }
}

static void HandleKey(KeyEvent_t event)
{
    if ((event == KEY_UP) || (event == KEY_DOWN))
    {
        if (s_ui_mode == WATER_UI_REALTIME)
        {
            s_ui_mode = WATER_UI_THRESHOLD;
            s_ui_dirty = 1U;
        }
        else if (s_ui_mode == WATER_UI_THRESHOLD)
        {
            s_ui_mode = WATER_UI_REALTIME;
            s_ui_dirty = 1U;
        }
    }
    else if (event == KEY_ENTER)
    {
        s_sample_requested = 1U;
    }
    else if (event == KEY_BACK)
    {
        SendConfig();
    }
}

#if (WATER_PROTOCOL == WATER_PROTOCOL_JSON)
static void HandleCommandLine(const char *line)
{
    char cmd[WATER_CMD_MAX];

    if (line == NULL)
    {
        return;
    }

    line = SkipSpaces(line);
    if (*line != '{')
    {
        return;
    }

    if (JsonGetCmd(line, cmd, sizeof(cmd)) == 0U)
    {
        SendAck("unknown", 0U, "bad_cmd");
        return;
    }

    if (strcmp(cmd, "set_sample") == 0)
    {
        uint32_t period_s;

        if ((JsonGetU32(line, "period_s", &period_s) == 0U) ||
            (period_s < 5U) || (period_s > 86400U))
        {
            SendAck(cmd, 0U, "bad_value");
            return;
        }

        s_cfg.sample_period_s = period_s;
        if (SaveConfigWithAck(cmd) != 0U)
        {
            SendAck(cmd, 1U, NULL);
        }
    }
    else if (strcmp(cmd, "set_threshold") == 0)
    {
        NodeConfig_t next = s_cfg;

        if ((JsonGetFloat(line, "ph_min", &next.ph_min) == 0U) ||
            (JsonGetFloat(line, "ph_max", &next.ph_max) == 0U) ||
            (JsonGetFloat(line, "temp_min", &next.temp_min) == 0U) ||
            (JsonGetFloat(line, "temp_max", &next.temp_max) == 0U) ||
            (JsonGetFloat(line, "turb_max", &next.turb_max) == 0U) ||
            (next.ph_min >= next.ph_max) ||
            (next.temp_min >= next.temp_max) ||
            (next.turb_max < 0.0f))
        {
            SendAck(cmd, 0U, "bad_value");
            return;
        }

        s_cfg = next;
        if (SaveConfigWithAck(cmd) != 0U)
        {
            SendAck(cmd, 1U, NULL);
        }
    }
    else if (strcmp(cmd, "set_cal") == 0)
    {
        NodeConfig_t next = s_cfg;

        if ((JsonGetFloat(line, "ph_k", &next.ph_k) == 0U) ||
            (JsonGetFloat(line, "ph_b", &next.ph_b) == 0U) ||
            (JsonGetFloat(line, "turb_a", &next.turb_a) == 0U) ||
            (JsonGetFloat(line, "turb_b", &next.turb_b) == 0U) ||
            (JsonGetFloat(line, "turb_c", &next.turb_c) == 0U) ||
            (IsFiniteRange(next.ph_k, -100.0f, 100.0f) == 0U) ||
            (IsFiniteRange(next.ph_b, -100.0f, 100.0f) == 0U) ||
            (IsFiniteRange(next.turb_a, -100000.0f, 100000.0f) == 0U) ||
            (IsFiniteRange(next.turb_b, -100000.0f, 100000.0f) == 0U) ||
            (IsFiniteRange(next.turb_c, -100000.0f, 100000.0f) == 0U))
        {
            SendAck(cmd, 0U, "bad_value");
            return;
        }

        s_cfg = next;
        if (SaveConfigWithAck(cmd) != 0U)
        {
            SendAck(cmd, 1U, NULL);
        }
    }
    else if (strcmp(cmd, "get_config") == 0)
    {
        SendConfig();
    }
    else if (strcmp(cmd, "sample_now") == 0)
    {
        s_sample_requested = 1U;
        SendAck(cmd, 1U, NULL);
    }
    else if (strcmp(cmd, "ping") == 0)
    {
        SendAck(cmd, 1U, NULL);
    }
    else
    {
        SendAck(cmd, 0U, "unknown_cmd");
    }
}
#else
static void HandleCommandLine(const char *line)
{
    char cmd[WATER_CMD_MAX];

    if (line == NULL)
    {
        return;
    }

    line = SkipSpaces(line);
    if (*line == '\0')
    {
        return;
    }

    if (CnGetCmd(line, cmd, sizeof(cmd)) == 0U)
    {
        SendAck(CN_CMD_UNKNOWN, 0U, "bad_cmd");
        return;
    }

    if (strcmp(cmd, CN_CMD_SET_SAMPLE) == 0)
    {
        uint32_t period_s;

        if ((CnGetU32ByKey(line, CN_KEY_PERIOD, &period_s) == 0U) &&
            (CnGetU32AfterCmd(line, &period_s) == 0U))
        {
            SendAck(cmd, 0U, "bad_value");
            return;
        }

        if ((period_s < 5U) || (period_s > 86400U))
        {
            SendAck(cmd, 0U, "bad_value");
            return;
        }

        s_cfg.sample_period_s = period_s;
        if (SaveConfigWithAck(cmd) != 0U)
        {
            SendAck(cmd, 1U, NULL);
        }
    }
    else if (strcmp(cmd, CN_CMD_SET_THRESHOLD) == 0)
    {
        NodeConfig_t next = s_cfg;

        if ((CnGetRangeFloat(line, CN_KEY_PH, &next.ph_min, &next.ph_max) == 0U) ||
            (CnGetRangeFloat(line, CN_KEY_TEMP, &next.temp_min, &next.temp_max) == 0U) ||
            (CnGetFloatByKey(line, CN_KEY_TURB, &next.turb_max) == 0U) ||
            (next.ph_min >= next.ph_max) ||
            (next.temp_min >= next.temp_max) ||
            (next.turb_max < 0.0f))
        {
            SendAck(cmd, 0U, "bad_value");
            return;
        }

        s_cfg = next;
        if (SaveConfigWithAck(cmd) != 0U)
        {
            SendAck(cmd, 1U, NULL);
        }
    }
    else if (strcmp(cmd, CN_CMD_SET_CAL) == 0)
    {
        NodeConfig_t next = s_cfg;

        if ((CnGetFloatByKey(line, CN_KEY_PHK, &next.ph_k) == 0U) ||
            (CnGetFloatByKey(line, CN_KEY_PHB, &next.ph_b) == 0U) ||
            (CnGetFloatByKey(line, CN_KEY_TURB_A, &next.turb_a) == 0U) ||
            (CnGetFloatByKey(line, CN_KEY_TURB_B, &next.turb_b) == 0U) ||
            (CnGetFloatByKey(line, CN_KEY_TURB_C, &next.turb_c) == 0U) ||
            (IsFiniteRange(next.ph_k, -100.0f, 100.0f) == 0U) ||
            (IsFiniteRange(next.ph_b, -100.0f, 100.0f) == 0U) ||
            (IsFiniteRange(next.turb_a, -100000.0f, 100000.0f) == 0U) ||
            (IsFiniteRange(next.turb_b, -100000.0f, 100000.0f) == 0U) ||
            (IsFiniteRange(next.turb_c, -100000.0f, 100000.0f) == 0U))
        {
            SendAck(cmd, 0U, "bad_value");
            return;
        }

        s_cfg = next;
        if (SaveConfigWithAck(cmd) != 0U)
        {
            SendAck(cmd, 1U, NULL);
        }
    }
    else if (strcmp(cmd, CN_CMD_GET_CONFIG) == 0)
    {
        SendConfig();
    }
    else if (strcmp(cmd, CN_CMD_SAMPLE_NOW) == 0)
    {
        s_sample_requested = 1U;
        SendAck(cmd, 1U, NULL);
    }
    else if (strcmp(cmd, CN_CMD_PING) == 0)
    {
        SendAck(cmd, 1U, NULL);
    }
    else
    {
        SendAck(cmd, 0U, "unknown_cmd");
    }
}
#endif

static void ServiceInputs(void)
{
    char line[LORA_LINE_MAX];
    KeyEvent_t event;

    while (Lora_ReadLine(line, sizeof(line)) != 0U)
    {
        HandleCommandLine(line);
    }

    do
    {
        event = Key_Scan();
        if (event != KEY_NONE)
        {
            HandleKey(event);
        }
    } while (event != KEY_NONE);
}

static void WaitWithService(uint32_t wait_ms, uint8_t break_on_sample_request, uint8_t render_ui)
{
    uint32_t start = HAL_GetTick();

    while ((HAL_GetTick() - start) < wait_ms)
    {
        ServiceInputs();
        UpdateAlarmLed();
        if (render_ui != 0U)
        {
            RenderUi();
        }

        if ((break_on_sample_request != 0U) && (s_sample_requested != 0U))
        {
            break;
        }

        delay_ms(WATER_UI_REFRESH_MS);
    }
}

static WaterSample_t TakeSample(void)
{
    WaterSample_t sample;

    memset(&sample, 0, sizeof(sample));
    sample.err = (uint8_t)(s_init_err & (WATER_ERR_ADC | WATER_ERR_LORA | WATER_ERR_LOW_POWER));

    if (DS18B20_StartConversion() == 0U)
    {
        sample.err |= WATER_ERR_TEMP;
        sample.temp_c = DS18B20_TEMP_ERROR;
    }
    else
    {
        WaitWithService(WATER_DS18B20_WAIT_MS, 0U, 0U);
        sample.temp_c = DS18B20_ReadScratchpadTemperature();
        if (sample.temp_c <= (DS18B20_TEMP_ERROR + 1.0f))
        {
            sample.err |= WATER_ERR_TEMP;
        }
    }

    sample.ph = PH_Read(s_cfg.ph_k, s_cfg.ph_b);
    sample.turb = Turbidity_Read(s_cfg.turb_a, s_cfg.turb_b, s_cfg.turb_c);
    sample.alarm = CalcAlarm(&sample);

    return sample;
}

static void RunOneCycle(void)
{
    WaterSample_t sample;
#if (WATER_DEBUG_MODE == 1)
    uint32_t start_tick = HAL_GetTick();
#endif

    s_sample_requested = 0U;
    if (s_lora_ready != 0U)
    {
        Lora_SetMode(LORA_MODE_NORMAL);
        Lora_RestartRxIT();
    }

    sample = TakeSample();
    s_seq++;
    s_last_alarm = sample.alarm;
    s_last_blink_tick = HAL_GetTick();
    s_alarm_led_on = (sample.alarm != 0U) ? 1U : 0U;
    s_last_sample = sample;
    s_last_sample_valid = 1U;
    s_ui_mode = WATER_UI_WAKE;
    s_ui_dirty = 1U;
    s_last_rendered_mode = 0xFFU;
    s_last_rendered_wake = 0U;
    s_wake_banner_end_tick = HAL_GetTick() + WATER_WAKE_SPLASH_MS;

    LED_Set(sample.alarm);
    SendTelemetry(&sample);

#if (WATER_DEBUG_MODE == 1)
    {
        uint32_t elapsed = HAL_GetTick() - start_tick;
        if (elapsed < WATER_DEBUG_PERIOD_MS)
        {
            WaitWithService(WATER_DEBUG_PERIOD_MS - elapsed, 0U, 1U);
        }
    }
    s_sample_requested = 1U;
#else
    WaitWithService(WATER_ACTIVE_WINDOW_MS, 1U, 1U);
#endif

    if (sample.alarm != 0U)
    {
        /* 报警未解除时继续保持在线，保证本地 LED 连续闪烁。 */
        s_sample_requested = 1U;
    }
}

static void EnterSleepWindow(void)
{
    uint32_t sleep_s = s_cfg.sample_period_s;
    uint8_t wake_by_rtc = 0U;

    DrawSleep(sleep_s);
    LED_Off();
    if (s_lora_ready != 0U)
    {
        Lora_SetMode(LORA_MODE_NORMAL);
    }

    if (LowPower_EnterStop(sleep_s) != 0U)
    {
        wake_by_rtc = LowPower_WasRtcAlarm();
        s_sleep_accum_s += sleep_s;
    }
    else
    {
        s_sleep_accum_s += 1U;
        delay_ms(1000U);
        wake_by_rtc = 1U;
    }

    sys_stm32_clock_init(RCC_PLL_MUL9);
    delay_init(72U);
    LowPower_ClearWakeFlag();
    if (s_lora_ready != 0U)
    {
        Lora_SetMode(LORA_MODE_NORMAL);
        Lora_RestartRxIT();
    }
    (void)BoardADC_Init();

    s_sample_requested = (wake_by_rtc != 0U) ? 1U : 0U;
}

void WaterNode_Init(void)
{
    uint8_t err = 0U;

    LED_Init();
    LED_Off();
    ConfigStorage_Load(&s_cfg);

    OLED_Init();
    OLED_Clear();
    OLED_ShowString(0, 0, "WaterNode", OLED_6X8);
    OLED_ShowString(0, 16, "Init", OLED_6X8);
    OLED_Update();

    Key_Init();
    if (BoardADC_Init() == 0U)
    {
        err |= WATER_ERR_ADC;
    }
    PH_Init();
    Turbidity_Init();
    if (DS18B20_Init() == 0U)
    {
        err |= WATER_ERR_TEMP;
    }
    if (Lora_Init() == 0U)
    {
        err |= WATER_ERR_LORA;
        s_lora_ready = 0U;
    }
    else
    {
        s_lora_ready = 1U;
    }
    if (LowPower_Init() == 0U)
    {
        err |= WATER_ERR_LOW_POWER;
    }

    if (err != 0U)
    {
        LED_On();
        OLED_ShowString(0, 32, "Err:", OLED_6X8);
        OLED_ShowNum(30, 32, err, 2U, OLED_6X8);
        OLED_Update();
    }

    s_init_err = err;
    s_sample_requested = 1U;
}

void WaterNode_Loop(void)
{
    ServiceInputs();

    if (s_sample_requested != 0U)
    {
        RunOneCycle();
    }

    if (s_sample_requested == 0U)
    {
        EnterSleepWindow();
    }
}
