#include "onenet.h"
#include "onenet_config.h"
#include "Hardware/AirM2M_4G/AirM2M_4G.h"
#include "stm32f1xx_hal.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static uint8_t s_onenet_inited = 0U;
static uint8_t s_onenet_ready = 0U;
static uint32_t s_last_connect_attempt = 0U;
static uint32_t s_last_status_poll = 0U;
static uint32_t s_message_id = 1U;
static uint8_t s_mqtt_host_index = 0U;

static char s_property_post_topic[ONENET_TOPIC_BUF_SIZE];

static const char *OneNet_GetMqttHost(void);
static void OneNet_SwitchMqttHost(void);
static uint8_t OneNet_BuildTopics(void);
static int OneNet_Append(char *buffer, uint16_t size, uint16_t *pos, const char *fmt, ...);
static int OneNet_AppendJsonString(char *buffer, uint16_t size, uint16_t *pos, const char *text);
static int OneNet_AppendFloat(char *buffer, uint16_t size, uint16_t *pos, float value, uint8_t precision);
static int OneNet_FormatPropertiesPayload(char *buffer, uint16_t size, const OneNetProperty_t *properties, uint8_t count);

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

static uint8_t OneNet_BuildTopics(void)
{
    int written;

    written = snprintf(s_property_post_topic,
                       sizeof(s_property_post_topic),
                       ONENET_PROPERTY_POST_TOPIC_TEMPLATE,
                       ONENET_PRODUCT_ID,
                       ONENET_DEVICE_NAME);
    if ((written < 0) || ((uint32_t)written >= sizeof(s_property_post_topic)))
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

uint8_t OneNet_Init(void)
{
    s_onenet_inited = 0U;
    s_onenet_ready = 0U;
    s_last_connect_attempt = HAL_GetTick() - ONENET_CONNECT_RETRY_MS;
    s_last_status_poll = HAL_GetTick();
    s_message_id = 1U;
    s_mqtt_host_index = 0U;

    if (OneNet_BuildTopics() == 0U)
    {
        return 0U;
    }

    AirM2M_4G_RegisterMqttMessageCallback(NULL);
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

    s_onenet_ready = 1U;

    if ((now - s_last_status_poll) >= ONENET_STATUS_POLL_MS)
    {
        s_last_status_poll = now;
        (void)AirM2M_4G_QueryMqttStatus();
    }
}

uint8_t OneNet_IsConnected(void)
{
    return AirM2M_4G_IsMqttConnected();
}

uint8_t OneNet_IsReady(void)
{
    return s_onenet_ready;
}

uint8_t OneNet_PublishProperties(const OneNetProperty_t *properties, uint8_t count)
{
    char payload[ONENET_PAYLOAD_BUF_SIZE];
    int payload_len;

    if ((properties == NULL) || (count == 0U) || (OneNet_IsConnected() == 0U))
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
