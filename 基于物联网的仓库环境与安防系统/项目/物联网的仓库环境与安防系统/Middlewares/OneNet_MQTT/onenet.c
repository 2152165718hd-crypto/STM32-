#include "onenet.h"

#include ".\\Hardware\\ESP_01S\\ESP_01S.h"
#include "MqttKit.h"
#include "cJSON.h"
#include "stm32f1xx_hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ONENET_PRODUCT_ID "FU4VqqGAT7"
#define ONENET_DEVICE_NAME "don1ng"
#define ONENET_TOKEN "version=2018-10-31&res=products%2FFU4VqqGAT7%2Fdevices%2Fdon1ng&et=1798736400&method=md5&sign=eMPJnRGGARuAqEBeifsuCw%3D%3D"
#define ONENET_PROPERTY_POST_TOPIC "$sys/FU4VqqGAT7/don1ng/thing/property/post"
#define ONENET_PROPERTY_SET_TOPIC "$sys/FU4VqqGAT7/don1ng/thing/property/set"
#define ONENET_PROPERTY_SET_REPLY_TOPIC "$sys/FU4VqqGAT7/don1ng/thing/property/set_reply"
#define ONENET_PROPERTY_SET_REPLY_METHOD "thing.property.set.reply"

#define ONENET_KEEPALIVE_SECONDS 60U
#define ONENET_CONNECT_RETRY_MS 5000U
#define ONENET_PING_INTERVAL_MS 30000U
#define ONENET_SET_REPLY_DELAY_MS 200U
#define ONENET_PAYLOAD_BUF_SIZE 256U
#define ONENET_SET_REPLY_BUF_SIZE 128U
#define ONENET_DEFAULT_REPORT_MS 2000U

static uint8_t onenet_mqtt_connected = 0U;
static uint8_t onenet_subscribed = 0U;
static uint32_t onenet_last_connect_attempt = 0U;
static uint32_t onenet_last_subscribe_attempt = 0U;
static uint32_t onenet_last_tx_tick = 0U;
static uint8_t onenet_pending_set_reply = 0U;
static uint16_t onenet_pending_set_reply_code = 0U;
static uint32_t onenet_pending_set_reply_tick = 0U;
static char onenet_pending_set_reply_id[24] = {0};

static uint32_t onenet_message_id = 1U;
static uint16_t onenet_packet_id = 1U;
static OneNet_AlarmControlCallback_t onenet_alarm_control_callback = NULL;

static OneNetTelemetry_t onenet_telemetry_cache = {0};
static uint8_t onenet_telemetry_valid = 0U;
static uint32_t onenet_report_interval_ms = ONENET_DEFAULT_REPORT_MS;
static uint32_t onenet_last_report_tick = 0U;

static uint16_t OneNet_NextPacketId(void);
static uint8_t OneNet_SendPacket(MQTT_PACKET_STRUCTURE *packet);
static uint8_t OneNet_SendConnect(void);
static uint8_t OneNet_SendSubscribe(void);
static uint8_t OneNet_SendPing(void);
static uint8_t OneNet_SendPublishResponse(uint8_t qos, uint16_t packet_id);
static uint8_t OneNet_SendPropertySetReply(const char *message_id, uint16_t code);
static void OneNet_RequestPropertySetReply(const char *message_id, uint16_t code);
static uint8_t OneNet_AddValueParam(cJSON *params, const char *identifier, cJSON *value);
static int OneNet_FormatPayload(char *buffer, uint16_t size, const OneNetTelemetry_t *data);
static uint8_t OneNet_ParseAlarmState(const cJSON *item, uint8_t *alarm_on);
static void OneNet_HandlePropertySetPayload(const char *payload);
static void OneNet_HandleMqttFrame(uint8_t *data, uint16_t len);
static void OneNet_RxCallback(ESP_DataPacket_t *packet);

static uint16_t OneNet_NextPacketId(void)
{
    uint16_t current = onenet_packet_id;

    onenet_packet_id++;
    if (onenet_packet_id == 0U)
    {
        onenet_packet_id = 1U;
    }

    return current;
}

static uint8_t OneNet_SendPacket(MQTT_PACKET_STRUCTURE *packet)
{
    uint8_t result = 0U;

    if ((packet == NULL) || (packet->_data == NULL) || (packet->_len == 0U))
    {
        return 0U;
    }

    if (ESP_SendData(0U, packet->_data, (uint16_t)packet->_len) == ESP_OK)
    {
        onenet_last_tx_tick = HAL_GetTick();
        result = 1U;
    }
    else
    {
        onenet_mqtt_connected = 0U;
        onenet_subscribed = 0U;
    }

    MQTT_DeleteBuffer(packet);
    return result;
}

static uint8_t OneNet_SendConnect(void)
{
    MQTT_PACKET_STRUCTURE packet = {0};

    onenet_last_connect_attempt = HAL_GetTick();

    if (MQTT_PacketConnect(ONENET_PRODUCT_ID,
                           ONENET_TOKEN,
                           ONENET_DEVICE_NAME,
                           ONENET_KEEPALIVE_SECONDS,
                           1,
                           MQTT_QOS_LEVEL0,
                           NULL,
                           NULL,
                           0,
                           &packet) != 0U)
    {
        return 0U;
    }

    return OneNet_SendPacket(&packet);
}

static uint8_t OneNet_SendSubscribe(void)
{
    MQTT_PACKET_STRUCTURE packet = {0};
    const int8 *topics[] = {ONENET_PROPERTY_SET_TOPIC};

    onenet_last_subscribe_attempt = HAL_GetTick();

    if (MQTT_PacketSubscribe(MQTT_SUBSCRIBE_ID,
                             MQTT_QOS_LEVEL0,
                             topics,
                             1U,
                             &packet) != 0U)
    {
        return 0U;
    }

    return OneNet_SendPacket(&packet);
}

static uint8_t OneNet_SendPing(void)
{
    MQTT_PACKET_STRUCTURE packet = {0};

    if (MQTT_PacketPing(&packet) != 0U)
    {
        return 0U;
    }

    return OneNet_SendPacket(&packet);
}

static uint8_t OneNet_SendPublishResponse(uint8_t qos, uint16_t packet_id)
{
    MQTT_PACKET_STRUCTURE packet = {0};

    if ((qos == MQTT_QOS_LEVEL0) || (packet_id == 0U))
    {
        return 1U;
    }

    if (qos == MQTT_QOS_LEVEL1)
    {
        if (MQTT_PacketPublishAck(packet_id, &packet) != 0U)
        {
            return 0U;
        }
    }
    else if (qos == MQTT_QOS_LEVEL2)
    {
        if (MQTT_PacketPublishRec(packet_id, &packet) != 0U)
        {
            return 0U;
        }
    }
    else
    {
        return 0U;
    }

    return OneNet_SendPacket(&packet);
}

static uint8_t OneNet_SendPropertySetReply(const char *message_id, uint16_t code)
{
    MQTT_PACKET_STRUCTURE packet = {0};
    char payload[ONENET_SET_REPLY_BUF_SIZE];
    const char *reply_id = (message_id != NULL) ? message_id : "0";
    const char *reply_msg = (code == 200U) ? "success" : "failed";
    int written = 0;

    written = snprintf(payload,
                       sizeof(payload),
                       "{\"id\":\"%s\",\"code\":%u,\"msg\":\"%s\",\"method\":\"%s\"}",
                       reply_id,
                       (unsigned int)code,
                       reply_msg,
                       ONENET_PROPERTY_SET_REPLY_METHOD);
    if ((written < 0) || ((uint32_t)written >= sizeof(payload)))
    {
        return 0U;
    }

    if (MQTT_PacketPublish(OneNet_NextPacketId(),
                           ONENET_PROPERTY_SET_REPLY_TOPIC,
                           payload,
                           (uint32_t)written,
                           MQTT_QOS_LEVEL1,
                           0,
                           0,
                           &packet) != 0U)
    {
        return 0U;
    }

    return OneNet_SendPacket(&packet);
}

static void OneNet_RequestPropertySetReply(const char *message_id, uint16_t code)
{
    const char *reply_id = (message_id != NULL) ? message_id : "0";

    memset(onenet_pending_set_reply_id, 0, sizeof(onenet_pending_set_reply_id));
    strncpy(onenet_pending_set_reply_id, reply_id, sizeof(onenet_pending_set_reply_id) - 1U);
    onenet_pending_set_reply_code = code;
    onenet_pending_set_reply_tick = HAL_GetTick();
    onenet_pending_set_reply = 1U;
}

static uint8_t OneNet_AddValueParam(cJSON *params, const char *identifier, cJSON *value)
{
    cJSON *node = NULL;

    if ((params == NULL) || (identifier == NULL) || (value == NULL))
    {
        if (value != NULL)
        {
            cJSON_Delete(value);
        }
        return 0U;
    }

    node = cJSON_CreateObject();
    if (node == NULL)
    {
        cJSON_Delete(value);
        return 0U;
    }

    cJSON_AddItemToObject(node, "value", value);
    cJSON_AddItemToObject(params, identifier, node);

    return 1U;
}

static int OneNet_FormatPayload(char *buffer, uint16_t size, const OneNetTelemetry_t *data)
{
    cJSON *root = NULL;
    cJSON *params = NULL;
    char *json_text = NULL;
    uint32_t message_id = onenet_message_id;
    int written = -1;

    if ((buffer == NULL) || (data == NULL) || (size == 0U))
    {
        return -1;
    }

    root = cJSON_CreateObject();
    params = cJSON_CreateObject();
    if ((root == NULL) || (params == NULL))
    {
        cJSON_Delete(root);
        cJSON_Delete(params);
        return -1;
    }

    snprintf(buffer, size, "%lu", (unsigned long)message_id);
    cJSON_AddStringToObject(root, "id", buffer);
    cJSON_AddStringToObject(root, "version", "1.0");
    cJSON_AddItemToObject(root, "params", params);

    if ((OneNet_AddValueParam(params, "AV_ALM", cJSON_CreateBool(data->av_alm != 0U)) == 0U) ||
        (OneNet_AddValueParam(params, "Hum", cJSON_CreateNumber((double)data->hum)) == 0U) ||
        (OneNet_AddValueParam(params, "PIR", cJSON_CreateBool(data->pir != 0U)) == 0U) ||
        (OneNet_AddValueParam(params, "Smoke", cJSON_CreateNumber((double)data->smoke)) == 0U) ||
        (OneNet_AddValueParam(params, "Temp", cJSON_CreateNumber((double)data->temp)) == 0U))
    {
        cJSON_Delete(root);
        return -1;
    }

    json_text = cJSON_PrintUnformatted(root);
    if (json_text != NULL)
    {
        written = (int)strlen(json_text);
        if ((written > 0) && ((uint32_t)written < size))
        {
            memcpy(buffer, json_text, (size_t)written + 1U);
        }
        else
        {
            written = -1;
        }
        free(json_text);
    }

    cJSON_Delete(root);

    if (written > 0)
    {
        onenet_message_id++;
        if (onenet_message_id == 0U)
        {
            onenet_message_id = 1U;
        }
    }

    return written;
}

static uint8_t OneNet_ParseAlarmState(const cJSON *item, uint8_t *alarm_on)
{
    if ((item == NULL) || (alarm_on == NULL))
    {
        return 0U;
    }

    if ((item->type & 0xFF) == cJSON_True)
    {
        *alarm_on = 1U;
        return 1U;
    }

    if ((item->type & 0xFF) == cJSON_False)
    {
        *alarm_on = 0U;
        return 1U;
    }

    if ((item->type & 0xFF) == cJSON_Number)
    {
        *alarm_on = (item->valueint != 0) ? 1U : 0U;
        return 1U;
    }

    if (((item->type & 0xFF) == cJSON_String) && (item->valuestring != NULL))
    {
        if ((strcmp(item->valuestring, "1") == 0) ||
            (strcmp(item->valuestring, "true") == 0) ||
            (strcmp(item->valuestring, "TRUE") == 0) ||
            (strcmp(item->valuestring, "on") == 0) ||
            (strcmp(item->valuestring, "ON") == 0))
        {
            *alarm_on = 1U;
            return 1U;
        }

        if ((strcmp(item->valuestring, "0") == 0) ||
            (strcmp(item->valuestring, "false") == 0) ||
            (strcmp(item->valuestring, "FALSE") == 0) ||
            (strcmp(item->valuestring, "off") == 0) ||
            (strcmp(item->valuestring, "OFF") == 0))
        {
            *alarm_on = 0U;
            return 1U;
        }
    }

    if ((item->type & 0xFF) == cJSON_Object)
    {
        return OneNet_ParseAlarmState(cJSON_GetObjectItem((cJSON *)item, "value"), alarm_on);
    }

    return 0U;
}

static void OneNet_HandlePropertySetPayload(const char *payload)
{
    cJSON *root = NULL;
    cJSON *params = NULL;
    cJSON *item = NULL;
    cJSON *id_item = NULL;
    char message_id[24];
    uint8_t alarm_on = 0U;
    uint8_t handled_any = 0U;
    uint8_t success = 1U;

    if (payload == NULL)
    {
        return;
    }

    memset(message_id, 0, sizeof(message_id));
    strncpy(message_id, "0", sizeof(message_id) - 1U);

    root = cJSON_Parse(payload);
    if (root == NULL)
    {
        return;
    }

    id_item = cJSON_GetObjectItem(root, "id");
    if (id_item != NULL)
    {
        if (((id_item->type & 0xFF) == cJSON_String) && (id_item->valuestring != NULL))
        {
            strncpy(message_id, id_item->valuestring, sizeof(message_id) - 1U);
            message_id[sizeof(message_id) - 1U] = '\0';
        }
        else if ((id_item->type & 0xFF) == cJSON_Number)
        {
            snprintf(message_id, sizeof(message_id), "%d", id_item->valueint);
        }
    }

    params = cJSON_GetObjectItem(root, "params");
    if ((params == NULL) || ((params->type & 0xFF) != cJSON_Object))
    {
        success = 0U;
    }
    else
    {
        for (item = params->child; item != NULL; item = item->next)
        {
            handled_any = 1U;

            if ((item->string != NULL) && (strcmp(item->string, "AV_ALM") == 0))
            {
                if ((OneNet_ParseAlarmState(item, &alarm_on) != 0U) &&
                    (onenet_alarm_control_callback != NULL))
                {
                    onenet_alarm_control_callback(alarm_on);
                }
                else
                {
                    success = 0U;
                }
            }
            else
            {
                success = 0U;
            }
        }
    }

    if (handled_any == 0U)
    {
        success = 0U;
    }

    OneNet_RequestPropertySetReply(message_id, (success != 0U) ? 200U : 400U);
    cJSON_Delete(root);
}

static void OneNet_HandleMqttFrame(uint8_t *data, uint16_t len)
{
    uint8_t packet_type = 0U;

    if ((data == NULL) || (len == 0U))
    {
        return;
    }

    packet_type = MQTT_UnPacketRecv(data);
    switch (packet_type)
    {
    case MQTT_PKT_CONNACK:
        if (MQTT_UnPacketConnectAck(data) == 0U)
        {
            onenet_mqtt_connected = 1U;
            onenet_subscribed = 0U;
            onenet_last_subscribe_attempt = HAL_GetTick() - ONENET_CONNECT_RETRY_MS;
            onenet_last_tx_tick = HAL_GetTick();
        }
        else
        {
            onenet_mqtt_connected = 0U;
            onenet_subscribed = 0U;
        }
        break;

    case MQTT_PKT_SUBACK:
        if (MQTT_UnPacketSubscribe(data) == 0U)
        {
            onenet_subscribed = 1U;
            onenet_last_tx_tick = HAL_GetTick();
        }
        else
        {
            onenet_subscribed = 0U;
        }
        break;

    case MQTT_PKT_PUBLISH:
    {
        int8 *topic = NULL;
        int8 *payload = NULL;
        uint16 topic_len = 0U;
        uint16 payload_len = 0U;
        uint16 packet_id = 0U;
        uint8 qos = 0U;

        if (MQTT_UnPacketPublish(data, &topic, &topic_len, &payload, &payload_len, &qos, &packet_id) == 0U)
        {
            (void)OneNet_SendPublishResponse(qos, packet_id);

            if ((topic != NULL) && (payload != NULL) && (strcmp((const char *)topic, ONENET_PROPERTY_SET_TOPIC) == 0))
            {
                OneNet_HandlePropertySetPayload((const char *)payload);
                onenet_last_tx_tick = HAL_GetTick();
            }
        }

        if (topic != NULL)
        {
            MQTT_FreeBuffer(topic);
        }
        if (payload != NULL)
        {
            MQTT_FreeBuffer(payload);
        }
        (void)topic_len;
        (void)payload_len;
        (void)packet_id;
        (void)qos;
        break;
    }

    case MQTT_PKT_PINGRESP:
        onenet_last_tx_tick = HAL_GetTick();
        break;

    default:
        break;
    }
}

static void OneNet_RxCallback(ESP_DataPacket_t *packet)
{
    if ((packet == NULL) || (packet->len == 0U))
    {
        return;
    }

    OneNet_HandleMqttFrame(packet->data, packet->len);
}

uint8_t OneNet_Init(void)
{
    onenet_mqtt_connected = 0U;
    onenet_subscribed = 0U;
    onenet_last_connect_attempt = HAL_GetTick() - ONENET_CONNECT_RETRY_MS;
    onenet_last_subscribe_attempt = HAL_GetTick() - ONENET_CONNECT_RETRY_MS;
    onenet_last_tx_tick = HAL_GetTick();
    onenet_message_id = 1U;
    onenet_packet_id = 1U;
    onenet_pending_set_reply = 0U;
    onenet_pending_set_reply_code = 0U;
    onenet_pending_set_reply_tick = 0U;
    memset(onenet_pending_set_reply_id, 0, sizeof(onenet_pending_set_reply_id));

    memset(&onenet_telemetry_cache, 0, sizeof(onenet_telemetry_cache));
    onenet_telemetry_valid = 0U;
    onenet_report_interval_ms = ONENET_DEFAULT_REPORT_MS;
    onenet_last_report_tick = HAL_GetTick();

    ESP_RegisterCallback(OneNet_RxCallback);

    if (ESP_Init() != ESP_OK)
    {
        return 0U;
    }

    return 1U;
}

void OneNet_RegisterAlarmControlCallback(OneNet_AlarmControlCallback_t cb)
{
    onenet_alarm_control_callback = cb;
}

void OneNet_Process(void)
{
    uint32_t now = HAL_GetTick();

    ESP_Process();

    if (ESP_IsConnected() == 0U)
    {
        onenet_mqtt_connected = 0U;
        onenet_subscribed = 0U;
        return;
    }

    if (onenet_mqtt_connected == 0U)
    {
        if ((now - onenet_last_connect_attempt) >= ONENET_CONNECT_RETRY_MS)
        {
            (void)OneNet_SendConnect();
        }
        return;
    }

    if (onenet_subscribed == 0U)
    {
        if ((now - onenet_last_subscribe_attempt) >= ONENET_CONNECT_RETRY_MS)
        {
            (void)OneNet_SendSubscribe();
        }
        return;
    }

    if (onenet_pending_set_reply != 0U)
    {
        if ((now - onenet_pending_set_reply_tick) < ONENET_SET_REPLY_DELAY_MS)
        {
            return;
        }

        if (OneNet_SendPropertySetReply(onenet_pending_set_reply_id, onenet_pending_set_reply_code) != 0U)
        {
            onenet_pending_set_reply = 0U;
            onenet_pending_set_reply_code = 0U;
            onenet_pending_set_reply_tick = 0U;
            memset(onenet_pending_set_reply_id, 0, sizeof(onenet_pending_set_reply_id));
        }
        return;
    }

    /* ---- 自动定时上报遥测数据 ---- */
    if ((onenet_report_interval_ms != 0U) && (onenet_telemetry_valid != 0U))
    {
        if ((now - onenet_last_report_tick) >= onenet_report_interval_ms)
        {
            onenet_last_report_tick = now;
            (void)OneNet_PublishTelemetry(&onenet_telemetry_cache);
        }
    }

    if ((now - onenet_last_tx_tick) >= ONENET_PING_INTERVAL_MS)
    {
        (void)OneNet_SendPing();
    }
}

uint8_t OneNet_IsConnected(void)
{
    if ((ESP_IsConnected() == 0U) || (onenet_mqtt_connected == 0U))
    {
        return 0U;
    }

    return 1U;
}

uint8_t OneNet_PublishTelemetry(const OneNetTelemetry_t *data)
{
    MQTT_PACKET_STRUCTURE packet = {0};
    char payload[ONENET_PAYLOAD_BUF_SIZE];
    int payload_len = 0;

    if ((data == NULL) || (OneNet_IsConnected() == 0U))
    {
        return 0U;
    }

    payload_len = OneNet_FormatPayload(payload, (uint16_t)sizeof(payload), data);
    if (payload_len <= 0)
    {
        return 0U;
    }

    if (MQTT_PacketPublish(OneNet_NextPacketId(),
                           ONENET_PROPERTY_POST_TOPIC,
                           payload,
                           (uint32_t)payload_len,
                           MQTT_QOS_LEVEL0,
                           0,
                           0,
                           &packet) != 0U)
    {
        return 0U;
    }

    return OneNet_SendPacket(&packet);
}

void OneNet_UpdateTelemetryCache(const OneNetTelemetry_t *data)
{
    if (data == NULL)
    {
        return;
    }

    memcpy(&onenet_telemetry_cache, data, sizeof(OneNetTelemetry_t));
    onenet_telemetry_valid = 1U;
}

void OneNet_SetReportInterval(uint32_t interval_ms)
{
    onenet_report_interval_ms = interval_ms;
}
