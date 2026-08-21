#include "onenet.h"
#include "onenet_config.h"
#include "cJSON.h"
#include "Hardware/AirM2M_4G/AirM2M_4G.h"
#include "stm32f1xx_hal.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ONENET_SET_REPLY_QUEUE_SIZE 4U
#define ONENET_SET_REPLY_ID_SIZE    32U
#define ONENET_SET_REPLY_MSG_SIZE   32U

typedef struct
{
    char id[ONENET_SET_REPLY_ID_SIZE];
    uint16_t code;
    char msg[ONENET_SET_REPLY_MSG_SIZE];
} OneNetSetReply_t;

static uint8_t s_onenet_inited = 0U;
static uint8_t s_onenet_ready = 0U;
static uint8_t s_property_set_subscribed = 0U;
static uint8_t s_property_post_reply_subscribed = 0U;
static uint32_t s_last_connect_attempt = 0U;
static uint32_t s_message_id = 1U;
static uint8_t s_mqtt_host_index = 0U;
static uint16_t s_last_post_reply_code = 0U;
static char s_last_post_reply_msg[ONENET_SET_REPLY_MSG_SIZE];
static OneNetPropertySetCallback_t s_property_set_callback = NULL;
static OneNetSetReply_t s_set_reply_queue[ONENET_SET_REPLY_QUEUE_SIZE];
static uint8_t s_set_reply_head = 0U;
static uint8_t s_set_reply_tail = 0U;
static uint8_t s_set_reply_count = 0U;

static char s_property_post_topic[ONENET_TOPIC_BUF_SIZE];
static char s_property_post_reply_topic[ONENET_TOPIC_BUF_SIZE];
static char s_property_set_topic[ONENET_TOPIC_BUF_SIZE];
static char s_property_set_reply_topic[ONENET_TOPIC_BUF_SIZE];

static const char *OneNet_GetMqttHost(void);
static void OneNet_SwitchMqttHost(void);
static uint8_t OneNet_BuildTopics(void);
static int OneNet_Append(char *buffer, uint16_t size, uint16_t *pos, const char *fmt, ...);
static int OneNet_AppendJsonString(char *buffer, uint16_t size, uint16_t *pos, const char *text);
static int OneNet_AppendFloat(char *buffer, uint16_t size, uint16_t *pos, float value, uint8_t precision);
static int OneNet_FormatPropertiesPayload(char *buffer, uint16_t size, const OneNetProperty_t *properties, uint8_t count);
static cJSON *OneNet_GetParamValue(cJSON *params, const char *name);
static uint8_t OneNet_ReadFloat(cJSON *item, float *value);
static uint8_t OneNet_ReadInt(cJSON *item, int32_t *value);
static uint8_t OneNet_PublishSetReply(const char *id, uint16_t code, const char *msg);
static uint8_t OneNet_QueueSetReply(const char *id, uint16_t code, const char *msg);
static uint8_t OneNet_ProcessSetReplyQueue(void);
static void OneNet_HandlePropertyPostReply(const char *payload, uint16_t payload_len);
static void OneNet_HandlePropertySet(const char *payload, uint16_t payload_len);
static void OneNet_HandleMqttMessage(const char *topic, const char *payload, uint16_t payload_len);

static const char *OneNet_GetMqttHost(void)
{
#ifdef ONENET_MQTT_FALLBACK_HOST
    if ((s_mqtt_host_index != 0U) && (strcmp(ONENET_MQTT_FALLBACK_HOST, ONENET_MQTT_HOST) != 0))
    {
        return ONENET_MQTT_FALLBACK_HOST;
    }
#endif
    return ONENET_MQTT_HOST;
}

static void OneNet_SwitchMqttHost(void)
{
#ifdef ONENET_MQTT_FALLBACK_HOST
    if (strcmp(ONENET_MQTT_FALLBACK_HOST, ONENET_MQTT_HOST) != 0)
    {
        s_mqtt_host_index = (s_mqtt_host_index == 0U) ? 1U : 0U;
    }
#endif
}

static uint8_t OneNet_FormatTopic(char *topic, uint16_t topic_size, const char *format)
{
    int written;

    written = snprintf(topic, topic_size, format, ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
    if ((written < 0) || ((uint32_t)written >= topic_size))
    {
        return 0U;
    }

    return 1U;
}

static uint8_t OneNet_BuildTopics(void)
{
    if (OneNet_FormatTopic(s_property_post_topic,
                           (uint16_t)sizeof(s_property_post_topic),
                           ONENET_PROPERTY_POST_TOPIC_TEMPLATE) == 0U)
    {
        return 0U;
    }

    if (OneNet_FormatTopic(s_property_post_reply_topic,
                           (uint16_t)sizeof(s_property_post_reply_topic),
                           ONENET_PROPERTY_POST_REPLY_TOPIC_TEMPLATE) == 0U)
    {
        return 0U;
    }

    if (OneNet_FormatTopic(s_property_set_topic,
                           (uint16_t)sizeof(s_property_set_topic),
                           ONENET_PROPERTY_SET_TOPIC_TEMPLATE) == 0U)
    {
        return 0U;
    }

    if (OneNet_FormatTopic(s_property_set_reply_topic,
                           (uint16_t)sizeof(s_property_set_reply_topic),
                           ONENET_PROPERTY_SET_REPLY_TOPIC_TEMPLATE) == 0U)
    {
        return 0U;
    }

    return 1U;
}

static int OneNet_Append(char *buffer, uint16_t size, uint16_t *pos, const char *fmt, ...)
{
    va_list args;
    int written;

    if ((buffer == NULL) || (pos == NULL) || (fmt == NULL) || (*pos >= size))
    {
        return -1;
    }

    va_start(args, fmt);
    written = vsnprintf(buffer + *pos, (uint32_t)size - *pos, fmt, args);
    va_end(args);

    if ((written < 0) || ((uint32_t)written >= ((uint32_t)size - *pos)))
    {
        return -1;
    }

    *pos = (uint16_t)(*pos + (uint16_t)written);
    return written;
}

static int OneNet_AppendJsonString(char *buffer, uint16_t size, uint16_t *pos, const char *text)
{
    char ch;

    if (OneNet_Append(buffer, size, pos, "\"") < 0)
    {
        return -1;
    }

    if (text != NULL)
    {
        while (*text != '\0')
        {
            ch = *text++;
            if ((ch == '\"') || (ch == '\\'))
            {
                if (OneNet_Append(buffer, size, pos, "\\%c", ch) < 0)
                {
                    return -1;
                }
            }
            else if (ch == '\r')
            {
                if (OneNet_Append(buffer, size, pos, "\\r") < 0)
                {
                    return -1;
                }
            }
            else if (ch == '\n')
            {
                if (OneNet_Append(buffer, size, pos, "\\n") < 0)
                {
                    return -1;
                }
            }
            else if (ch == '\t')
            {
                if (OneNet_Append(buffer, size, pos, "\\t") < 0)
                {
                    return -1;
                }
            }
            else if ((uint8_t)ch >= 0x20U)
            {
                if (OneNet_Append(buffer, size, pos, "%c", ch) < 0)
                {
                    return -1;
                }
            }
        }
    }

    return OneNet_Append(buffer, size, pos, "\"");
}

static int OneNet_AppendFloat(char *buffer, uint16_t size, uint16_t *pos, float value, uint8_t precision)
{
    uint32_t scale = 1U;
    uint32_t scaled;
    uint32_t whole;
    uint32_t fraction;
    uint8_t i;

    if (precision > 6U)
    {
        precision = 6U;
    }
    for (i = 0U; i < precision; i++)
    {
        scale *= 10U;
    }

    if (value < 0.0f)
    {
        if (OneNet_Append(buffer, size, pos, "-") < 0)
        {
            return -1;
        }
        value = -value;
    }

    scaled = (uint32_t)(value * (float)scale + 0.5f);
    whole = scaled / scale;
    fraction = scaled % scale;

    if (OneNet_Append(buffer, size, pos, "%lu", (unsigned long)whole) < 0)
    {
        return -1;
    }

    if (precision == 0U)
    {
        return 0;
    }

    if (OneNet_Append(buffer, size, pos, ".") < 0)
    {
        return -1;
    }

    scale /= 10U;
    while (scale > 0U)
    {
        if (OneNet_Append(buffer, size, pos, "%lu", (unsigned long)((fraction / scale) % 10U)) < 0)
        {
            return -1;
        }
        scale /= 10U;
    }

    return 0;
}

static int OneNet_FormatPropertiesPayload(char *buffer, uint16_t size, const OneNetProperty_t *properties, uint8_t count)
{
    uint16_t pos = 0U;
    uint8_t i;
    uint32_t message_id = s_message_id;

    if ((buffer == NULL) || (properties == NULL) || (count == 0U) || (size == 0U))
    {
        return -1;
    }

    if (OneNet_Append(buffer, size, &pos, "{\"id\":\"%lu\",\"version\":\"1.0\",\"params\":{", (unsigned long)message_id) < 0)
    {
        return -1;
    }

    for (i = 0U; i < count; i++)
    {
        if ((properties[i].name == NULL) || (properties[i].name[0] == '\0'))
        {
            return -1;
        }

        if (i > 0U)
        {
            if (OneNet_Append(buffer, size, &pos, ",") < 0)
            {
                return -1;
            }
        }

        if (OneNet_AppendJsonString(buffer, size, &pos, properties[i].name) < 0)
        {
            return -1;
        }
        if (OneNet_Append(buffer, size, &pos, ":{\"value\":") < 0)
        {
            return -1;
        }

        switch (properties[i].type)
        {
        case ONENET_VALUE_INT:
            if (OneNet_Append(buffer, size, &pos, "%ld", (long)properties[i].value.i32) < 0)
            {
                return -1;
            }
            break;

        case ONENET_VALUE_FLOAT:
            if (OneNet_AppendFloat(buffer, size, &pos, properties[i].value.f32, properties[i].precision) < 0)
            {
                return -1;
            }
            break;

        case ONENET_VALUE_BOOL:
            if (OneNet_Append(buffer, size, &pos, "%s", (properties[i].value.boolean != 0U) ? "true" : "false") < 0)
            {
                return -1;
            }
            break;

        case ONENET_VALUE_STRING:
            if (OneNet_AppendJsonString(buffer, size, &pos, properties[i].value.str) < 0)
            {
                return -1;
            }
            break;

        default:
            return -1;
        }

        if (OneNet_Append(buffer, size, &pos, "}") < 0)
        {
            return -1;
        }
    }

    if (OneNet_Append(buffer, size, &pos, "}}") < 0)
    {
        return -1;
    }

    s_message_id++;
    if (s_message_id == 0U)
    {
        s_message_id = 1U;
    }

    return (int)pos;
}

static cJSON *OneNet_GetParamValue(cJSON *params, const char *name)
{
    cJSON *item;
    cJSON *value;

    if ((params == NULL) || (name == NULL))
    {
        return NULL;
    }

    item = cJSON_GetObjectItem(params, name);
    if (item == NULL)
    {
        return NULL;
    }

    if ((item->type & 0xFF) == cJSON_Object)
    {
        value = cJSON_GetObjectItem(item, "value");
        if (value != NULL)
        {
            return value;
        }
    }

    return item;
}

static uint8_t OneNet_ReadFloat(cJSON *item, float *value)
{
    if ((item == NULL) || (value == NULL))
    {
        return 0U;
    }

    if ((item->type & 0xFF) == cJSON_Number)
    {
        *value = (float)item->valuedouble;
        return 1U;
    }

    if (((item->type & 0xFF) == cJSON_String) && (item->valuestring != NULL))
    {
        *value = (float)atof(item->valuestring);
        return 1U;
    }

    return 0U;
}

static uint8_t OneNet_ReadInt(cJSON *item, int32_t *value)
{
    if ((item == NULL) || (value == NULL))
    {
        return 0U;
    }

    if ((item->type & 0xFF) == cJSON_Number)
    {
        *value = (int32_t)item->valueint;
        return 1U;
    }

    if (((item->type & 0xFF) == cJSON_String) && (item->valuestring != NULL))
    {
        *value = (int32_t)atoi(item->valuestring);
        return 1U;
    }

    return 0U;
}

static uint8_t OneNet_PublishSetReply(const char *id, uint16_t code, const char *msg)
{
    char payload[ONENET_PAYLOAD_BUF_SIZE];
    uint16_t pos = 0U;

    if ((id == NULL) || (msg == NULL) || (OneNet_IsConnected() == 0U))
    {
        return 0U;
    }

    if (OneNet_Append(payload, (uint16_t)sizeof(payload), &pos, "{\"id\":") < 0)
    {
        return 0U;
    }
    if (OneNet_AppendJsonString(payload, (uint16_t)sizeof(payload), &pos, id) < 0)
    {
        return 0U;
    }
    if (OneNet_Append(payload, (uint16_t)sizeof(payload), &pos, ",\"code\":%u,\"msg\":", (unsigned int)code) < 0)
    {
        return 0U;
    }
    if (OneNet_AppendJsonString(payload, (uint16_t)sizeof(payload), &pos, msg) < 0)
    {
        return 0U;
    }
    if (OneNet_Append(payload, (uint16_t)sizeof(payload), &pos, "}") < 0)
    {
        return 0U;
    }

    return (AirM2M_4G_MqttPublish(s_property_set_reply_topic,
                                  payload,
                                  ONENET_PROPERTY_SET_QOS,
                                  0U) == AIRM2M_4G_OK)
               ? 1U
               : 0U;
}

static uint8_t OneNet_QueueSetReply(const char *id, uint16_t code, const char *msg)
{
    OneNetSetReply_t *entry;

    if ((id == NULL) || (msg == NULL) || (id[0] == '\0'))
    {
        return 0U;
    }

    if (s_set_reply_count >= ONENET_SET_REPLY_QUEUE_SIZE)
    {
        s_set_reply_head = (uint8_t)((s_set_reply_head + 1U) % ONENET_SET_REPLY_QUEUE_SIZE);
        s_set_reply_count--;
    }

    entry = &s_set_reply_queue[s_set_reply_tail];
    memset(entry, 0, sizeof(*entry));
    strncpy(entry->id, id, sizeof(entry->id) - 1U);
    strncpy(entry->msg, msg, sizeof(entry->msg) - 1U);
    entry->code = code;

    s_set_reply_tail = (uint8_t)((s_set_reply_tail + 1U) % ONENET_SET_REPLY_QUEUE_SIZE);
    s_set_reply_count++;
    return 1U;
}

static uint8_t OneNet_ProcessSetReplyQueue(void)
{
    OneNetSetReply_t *entry;

    if ((s_set_reply_count == 0U) || (OneNet_IsConnected() == 0U))
    {
        return 0U;
    }

    entry = &s_set_reply_queue[s_set_reply_head];
    if (OneNet_PublishSetReply(entry->id, entry->code, entry->msg) == 0U)
    {
        return 0U;
    }

    memset(entry, 0, sizeof(*entry));
    s_set_reply_head = (uint8_t)((s_set_reply_head + 1U) % ONENET_SET_REPLY_QUEUE_SIZE);
    s_set_reply_count--;
    return 1U;
}

static void OneNet_HandlePropertyPostReply(const char *payload, uint16_t payload_len)
{
    char json_buf[ONENET_PAYLOAD_BUF_SIZE];
    cJSON *root;
    cJSON *code_item;
    cJSON *msg_item;

    if ((payload == NULL) || (payload_len == 0U))
    {
        return;
    }

    if (payload_len >= sizeof(json_buf))
    {
        payload_len = (uint16_t)(sizeof(json_buf) - 1U);
    }
    memcpy(json_buf, payload, payload_len);
    json_buf[payload_len] = '\0';

    root = cJSON_Parse(json_buf);
    if (root == NULL)
    {
        s_last_post_reply_code = 0U;
        strncpy(s_last_post_reply_msg, "bad json", sizeof(s_last_post_reply_msg) - 1U);
        s_last_post_reply_msg[sizeof(s_last_post_reply_msg) - 1U] = '\0';
        return;
    }

    code_item = cJSON_GetObjectItem(root, "code");
    msg_item = cJSON_GetObjectItem(root, "msg");

    s_last_post_reply_code = 0U;
    memset(s_last_post_reply_msg, 0, sizeof(s_last_post_reply_msg));

    if ((code_item != NULL) && ((code_item->type & 0xFF) == cJSON_Number))
    {
        s_last_post_reply_code = (uint16_t)code_item->valueint;
    }

    if ((msg_item != NULL) && ((msg_item->type & 0xFF) == cJSON_String) &&
        (msg_item->valuestring != NULL))
    {
        strncpy(s_last_post_reply_msg, msg_item->valuestring, sizeof(s_last_post_reply_msg) - 1U);
    }

    cJSON_Delete(root);
}

static void OneNet_HandlePropertySet(const char *payload, uint16_t payload_len)
{
    char json_buf[ONENET_PAYLOAD_BUF_SIZE];
    char id_buf[32];
    cJSON *root;
    cJSON *params;
    cJSON *id_item;
    cJSON *threshold_item;
    cJSON *period_item;
    float threshold = 0.0f;
    int32_t report_period = 0;
    uint8_t has_threshold = 0U;
    uint8_t has_report_period = 0U;
    uint8_t accepted = 1U;

    if ((payload == NULL) || (payload_len == 0U))
    {
        return;
    }

    if (payload_len >= sizeof(json_buf))
    {
        payload_len = (uint16_t)(sizeof(json_buf) - 1U);
    }
    memcpy(json_buf, payload, payload_len);
    json_buf[payload_len] = '\0';

    snprintf(id_buf, sizeof(id_buf), "%lu", (unsigned long)s_message_id);

    root = cJSON_Parse(json_buf);
    if (root == NULL)
    {
        (void)OneNet_QueueSetReply(id_buf, 400U, "bad json");
        return;
    }

    id_item = cJSON_GetObjectItem(root, "id");
    if (((id_item != NULL) && ((id_item->type & 0xFF) == cJSON_String)) && (id_item->valuestring != NULL))
    {
        strncpy(id_buf, id_item->valuestring, sizeof(id_buf) - 1U);
        id_buf[sizeof(id_buf) - 1U] = '\0';
    }
    else if ((id_item != NULL) && ((id_item->type & 0xFF) == cJSON_Number))
    {
        snprintf(id_buf, sizeof(id_buf), "%ld", (long)id_item->valueint);
    }

    params = cJSON_GetObjectItem(root, "params");
    if ((params == NULL) || ((params->type & 0xFF) != cJSON_Object))
    {
        (void)OneNet_QueueSetReply(id_buf, 400U, "bad params");
        cJSON_Delete(root);
        return;
    }

    threshold_item = OneNet_GetParamValue(params, ONENET_PROPERTY_TEMPERATURE_THRESHOLD);
    period_item = OneNet_GetParamValue(params, ONENET_PROPERTY_REPORT_PERIOD);

    has_threshold = OneNet_ReadFloat(threshold_item, &threshold);
    has_report_period = OneNet_ReadInt(period_item, &report_period);

    if (((has_threshold != 0U) || (has_report_period != 0U)) && (s_property_set_callback != NULL))
    {
        accepted = s_property_set_callback(has_threshold, threshold, has_report_period, report_period);
    }
    else if (((has_threshold != 0U) || (has_report_period != 0U)) && (s_property_set_callback == NULL))
    {
        accepted = 0U;
    }

    (void)OneNet_QueueSetReply(id_buf,
                               (accepted != 0U) ? 200U : 400U,
                               (accepted != 0U) ? "success" : "rejected");
    cJSON_Delete(root);
}

static void OneNet_HandleMqttMessage(const char *topic, const char *payload, uint16_t payload_len)
{
    if ((topic == NULL) || (payload == NULL))
    {
        return;
    }

    if (strcmp(topic, s_property_post_reply_topic) == 0)
    {
        OneNet_HandlePropertyPostReply(payload, payload_len);
    }
    else if (strcmp(topic, s_property_set_topic) == 0)
    {
        OneNet_HandlePropertySet(payload, payload_len);
    }
}

uint8_t OneNet_Init(void)
{
    s_onenet_inited = 0U;
    s_onenet_ready = 0U;
    s_property_set_subscribed = 0U;
    s_property_post_reply_subscribed = 0U;
    s_last_connect_attempt = HAL_GetTick() - ONENET_CONNECT_RETRY_MS;
    s_message_id = 1U;
    s_mqtt_host_index = 0U;
    s_last_post_reply_code = 0U;
    s_set_reply_head = 0U;
    s_set_reply_tail = 0U;
    s_set_reply_count = 0U;
    memset(s_last_post_reply_msg, 0, sizeof(s_last_post_reply_msg));
    memset(s_set_reply_queue, 0, sizeof(s_set_reply_queue));

    if (OneNet_BuildTopics() == 0U)
    {
        return 0U;
    }

    AirM2M_4G_RegisterMqttMessageCallback(OneNet_HandleMqttMessage);
    if (AirM2M_4G_Init() != AIRM2M_4G_OK)
    {
        return 0U;
    }

    s_onenet_inited = 1U;
    return 1U;
}

void OneNet_Process(void)
{
    uint32_t now = HAL_GetTick();
    AirM2M_4G_Result_t connect_result;

    AirM2M_4G_Process();
    if (s_onenet_inited == 0U)
    {
        return;
    }

    if (AirM2M_4G_IsMqttConnected() == 0U)
    {
        s_onenet_ready = 0U;
        s_property_set_subscribed = 0U;
        s_property_post_reply_subscribed = 0U;
        s_last_post_reply_code = 0U;
        memset(s_last_post_reply_msg, 0, sizeof(s_last_post_reply_msg));
        if ((now - s_last_connect_attempt) >= ONENET_CONNECT_RETRY_MS)
        {
            s_last_connect_attempt = now;
            connect_result = AirM2M_4G_MqttConnect(OneNet_GetMqttHost(),
                                                   ONENET_MQTT_PORT,
                                                   ONENET_DEVICE_NAME,
                                                   ONENET_PRODUCT_ID,
                                                   ONENET_TOKEN,
                                                   ONENET_KEEPALIVE_SECONDS,
                                                   ONENET_MQTT_CLEAN_SESSION);
            if (connect_result != AIRM2M_4G_OK)
            {
                OneNet_SwitchMqttHost();
            }
        }
        return;
    }

    if (s_property_set_subscribed == 0U)
    {
        s_onenet_ready = 0U;
        if (AirM2M_4G_MqttSubscribe(s_property_set_topic, ONENET_PROPERTY_SET_QOS) == AIRM2M_4G_OK)
        {
            s_property_set_subscribed = 1U;
            s_onenet_ready = 1U;
        }
        return;
    }

    if (s_property_post_reply_subscribed == 0U)
    {
        s_onenet_ready = 0U;
        if (AirM2M_4G_MqttSubscribe(s_property_post_reply_topic, ONENET_PROPERTY_POST_QOS) == AIRM2M_4G_OK)
        {
            s_property_post_reply_subscribed = 1U;
            s_onenet_ready = 1U;
        }
        return;
    }

    s_onenet_ready = 1U;
    if (OneNet_ProcessSetReplyQueue() != 0U)
    {
        return;
    }

    (void)now;
}

uint8_t OneNet_IsConnected(void)
{
    return AirM2M_4G_IsMqttConnected();
}

uint8_t OneNet_IsReady(void)
{
    return s_onenet_ready;
}

uint16_t OneNet_GetLastPostReplyCode(void)
{
    return s_last_post_reply_code;
}

const char *OneNet_GetLastPostReplyMsg(void)
{
    return s_last_post_reply_msg;
}

void OneNet_RegisterPropertySetCallback(OneNetPropertySetCallback_t callback)
{
    s_property_set_callback = callback;
}

uint8_t OneNet_PublishProperties(const OneNetProperty_t *properties, uint8_t count)
{
    char payload[ONENET_PAYLOAD_BUF_SIZE];
    int payload_len;

    if ((properties == NULL) || (count == 0U) || (OneNet_IsReady() == 0U))
    {
        return 0U;
    }

    payload_len = OneNet_FormatPropertiesPayload(payload, (uint16_t)sizeof(payload), properties, count);
    if (payload_len <= 0)
    {
        return 0U;
    }

    return (AirM2M_4G_MqttPublish(s_property_post_topic,
                                  payload,
                                  ONENET_PROPERTY_POST_QOS,
                                  0U) == AIRM2M_4G_OK)
               ? 1U
               : 0U;
}
