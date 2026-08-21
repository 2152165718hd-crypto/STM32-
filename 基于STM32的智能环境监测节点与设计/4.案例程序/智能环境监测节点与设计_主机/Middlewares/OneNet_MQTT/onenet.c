#include "onenet.h"

#include ".\\Hardware\\ESP_01S\\ESP_01S.h"
#include "MqttKit.h"
#include "cJSON.h"
#include "onenet_config.h"
#include "stm32f1xx_hal.h"

#include <stdio.h>
#include <string.h>

#define ONENET_PROPERTY_POST_TOPIC "$sys/" ONENET_PRODUCT_ID "/" ONENET_DEVICE_NAME "/thing/property/post"
#define ONENET_PROPERTY_SET_TOPIC "$sys/" ONENET_PRODUCT_ID "/" ONENET_DEVICE_NAME "/thing/property/set"
#define ONENET_PROPERTY_SET_REPLY_TOPIC "$sys/" ONENET_PRODUCT_ID "/" ONENET_DEVICE_NAME "/thing/property/set_reply"
#define ONENET_PROPERTY_SET_REPLY_METHOD "thing.property.set.reply"

#define ONENET_KEEPALIVE_SECONDS 60U
#define ONENET_CONNECT_RETRY_MS 5000U
#define ONENET_PING_INTERVAL_MS 30000U
#define ONENET_SET_REPLY_DELAY_MS 200U
#define ONENET_SET_REPLY_RETRY_MS 1000U
#define ONENET_SET_REPLY_MAX_ATTEMPTS 3U
#define ONENET_PAYLOAD_BUF_SIZE 768U
#define ONENET_SET_REPLY_BUF_SIZE 128U
#define ONENET_DEFAULT_REPORT_MS 2000U
#define ONENET_RX_QUEUE_DEPTH 3U

#define ONENET_REPLY_CODE_OK 200U
#define ONENET_REPLY_CODE_BAD_REQUEST 400U
#define ONENET_SEND_FAIL 0U
#define ONENET_SEND_OK 1U
#define ONENET_SEND_BUSY 2U

typedef struct
{
    uint8_t data[ESP_DATA_BUF_SIZE];
    uint16_t len;
} OneNetRxFrame_t;

static uint8_t onenet_mqtt_connected = 0U;
static uint8_t onenet_subscribed = 0U;
static uint32_t onenet_last_connect_attempt = 0U;
static uint32_t onenet_last_subscribe_attempt = 0U;
static uint32_t onenet_last_tx_tick = 0U;
static uint8_t onenet_pending_set_reply = 0U;
static uint16_t onenet_pending_set_reply_code = 0U;
static uint32_t onenet_pending_set_reply_tick = 0U;
static uint8_t onenet_pending_set_reply_attempts = 0U;
static char onenet_pending_set_reply_id[24] = {0};

static uint32_t onenet_message_id = 1U;
static uint16_t onenet_packet_id = 1U;
static OneNet_ThresholdCallback_t onenet_threshold_callback = NULL;

static OneNetTelemetry_t onenet_telemetry_cache = {0};
static uint8_t onenet_telemetry_valid = 0U;
static uint32_t onenet_report_interval_ms = ONENET_DEFAULT_REPORT_MS;
static uint32_t onenet_last_report_tick = 0U;
static OneNetRxFrame_t onenet_rx_queue[ONENET_RX_QUEUE_DEPTH];
static uint8_t onenet_rx_head = 0U;
static uint8_t onenet_rx_tail = 0U;
static uint8_t onenet_rx_count = 0U;

static uint16_t OneNet_NextPacketId(void);
static uint8_t OneNet_SendPacket(MQTT_PACKET_STRUCTURE *packet);
static uint8_t OneNet_SendConnect(void);
static uint8_t OneNet_SendSubscribe(void);
static uint8_t OneNet_SendPing(void);
static uint8_t OneNet_SendPublishResponse(uint8_t qos, uint16_t packet_id);
static uint8_t OneNet_SendPropertySetReply(const char *message_id, uint16_t code);
static void OneNet_RequestPropertySetReply(const char *message_id, uint16_t code);
static void OneNet_ClearPropertySetReply(void);
static uint8_t OneNet_ReadIntValue(const cJSON *item, int32_t min_value, int32_t max_value, int32_t *out_value);
static uint8_t OneNet_ApplyThresholdParam(OneNetThresholdUpdate_t *update, const char *name, const cJSON *item);
static int OneNet_FormatPayload(char *buffer, uint16_t size, const OneNetTelemetry_t *data);
static void OneNet_HandlePropertySetPayload(const char *payload);
static void OneNet_HandleMqttFrame(uint8_t *data, uint16_t len);
static void OneNet_RxCallback(ESP_DataPacket_t *packet);
static void OneNet_QueueRxFrame(const uint8_t *data, uint16_t len);
static void OneNet_ProcessRxQueue(void);

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
    ESP_Status_t status = ESP_ERR_SEND_FAIL;

    if ((packet == NULL) || (packet->_data == NULL) || (packet->_len == 0U))
    {
        return ONENET_SEND_FAIL;
    }

    status = ESP_SendDataAsync(0U, packet->_data, (uint16_t)packet->_len);
    if (status == ESP_OK)
    {
        onenet_last_tx_tick = HAL_GetTick();
        result = ONENET_SEND_OK;
    }
    else if (status == ESP_ERR_BUSY)
    {
        result = ONENET_SEND_BUSY;
    }
    else
    {
        onenet_mqtt_connected = 0U;
        onenet_subscribed = 0U;
        result = ONENET_SEND_FAIL;
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
    const char *reply_msg = (code == ONENET_REPLY_CODE_OK) ? "success" : "failed";
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
    onenet_pending_set_reply_attempts = 0U;
    onenet_pending_set_reply = 1U;
}

static void OneNet_ClearPropertySetReply(void)
{
    onenet_pending_set_reply = 0U;
    onenet_pending_set_reply_code = 0U;
    onenet_pending_set_reply_tick = 0U;
    onenet_pending_set_reply_attempts = 0U;
    memset(onenet_pending_set_reply_id, 0, sizeof(onenet_pending_set_reply_id));
}

static uint8_t OneNet_ReadIntValue(const cJSON *item, int32_t min_value, int32_t max_value, int32_t *out_value)
{
    int32_t value = 0;
    cJSON *value_node = NULL;

    if ((item == NULL) || (out_value == NULL))
    {
        return 0U;
    }

    if ((item->type & 0xFF) == cJSON_Object)
    {
        value_node = cJSON_GetObjectItem((cJSON *)item, "value");
        if (value_node == NULL)
        {
            return 0U;
        }
        return OneNet_ReadIntValue(value_node, min_value, max_value, out_value);
    }

    if ((item->type & 0xFF) == cJSON_Number)
    {
        value = item->valueint;
    }
    else if (((item->type & 0xFF) == cJSON_String) && (item->valuestring != NULL))
    {
        long parsed = 0;

        if (sscanf(item->valuestring, "%ld", &parsed) != 1)
        {
            return 0U;
        }
        value = (int32_t)parsed;
    }
    else
    {
        return 0U;
    }

    if ((value < min_value) || (value > max_value))
    {
        return 0U;
    }

    *out_value = value;
    return 1U;
}

static uint8_t OneNet_ApplyThresholdParam(OneNetThresholdUpdate_t *update, const char *name, const cJSON *item)
{
    int32_t value = 0;

    if ((update == NULL) || (name == NULL) || (item == NULL))
    {
        return 0U;
    }

    if (strcmp(name, "temperature_threshold") == 0)
    {
        if (OneNet_ReadIntValue(item, 0, 50, &value) == 0U)
        {
            return 0U;
        }
        update->temperature_threshold = value;
        update->has_temperature_threshold = 1U;
        return 1U;
    }

    if (strcmp(name, "humidity_threshold") == 0)
    {
        if (OneNet_ReadIntValue(item, 0, 100, &value) == 0U)
        {
            return 0U;
        }
        update->humidity_threshold = value;
        update->has_humidity_threshold = 1U;
        return 1U;
    }

    if (strcmp(name, "pm25_threshold") == 0)
    {
        if (OneNet_ReadIntValue(item, 0, 999, &value) == 0U)
        {
            return 0U;
        }
        update->pm25_threshold = value;
        update->has_pm25_threshold = 1U;
        return 1U;
    }

    if (strcmp(name, "gas_threshold") == 0)
    {
        if (OneNet_ReadIntValue(item, 0, 100, &value) == 0U)
        {
            return 0U;
        }
        update->gas_threshold = value;
        update->has_gas_threshold = 1U;
        return 1U;
    }

    if (strcmp(name, "light_threshold") == 0)
    {
        if (OneNet_ReadIntValue(item, 0, 9999, &value) == 0U)
        {
            return 0U;
        }
        update->light_threshold = value;
        update->has_light_threshold = 1U;
        return 1U;
    }

    return 0U;
}

static int OneNet_FormatPayload(char *buffer, uint16_t size, const OneNetTelemetry_t *data)
{
    uint32_t message_id = onenet_message_id;
    int written = -1;

    if ((buffer == NULL) || (data == NULL) || (size == 0U))
    {
        return -1;
    }

    written = snprintf(buffer,
                       size,
                       "{\"id\":\"%lu\",\"version\":\"1.0\",\"params\":{"
                       "\"temperature\":{\"value\":%ld},"
                       "\"humidity\":{\"value\":%ld},"
                       "\"pm25\":{\"value\":%ld},"
                       "\"mq135_mv\":{\"value\":%ld},"
                       "\"gas_percent\":{\"value\":%ld},"
                       "\"light_lux\":{\"value\":%ld},"
                       "\"valid_bits\":{\"value\":%ld},"
                       "\"slave_online\":{\"value\":%s},"
                       "\"alarm_active\":{\"value\":%s},"
                       "\"alarm_mask\":{\"value\":%ld},"
                       "\"temperature_threshold\":{\"value\":%ld},"
                       "\"humidity_threshold\":{\"value\":%ld},"
                       "\"pm25_threshold\":{\"value\":%ld},"
                       "\"gas_threshold\":{\"value\":%ld},"
                       "\"light_threshold\":{\"value\":%ld}"
                       "}}",
                       (unsigned long)message_id,
                       (long)data->temperature,
                       (long)data->humidity,
                       (long)data->pm25,
                       (long)data->mq135_mv,
                       (long)data->gas_percent,
                       (long)data->light_lux,
                       (long)data->valid_bits,
                       (data->slave_online != 0U) ? "true" : "false",
                       (data->alarm_active != 0U) ? "true" : "false",
                       (long)data->alarm_mask,
                       (long)data->temperature_threshold,
                       (long)data->humidity_threshold,
                       (long)data->pm25_threshold,
                       (long)data->gas_threshold,
                       (long)data->light_threshold);

    if ((written < 0) || ((uint32_t)written >= size))
    {
        return -1;
    }

    onenet_message_id++;
    if (onenet_message_id == 0U)
    {
        onenet_message_id = 1U;
    }

    return written;
}

static void OneNet_HandlePropertySetPayload(const char *payload)
{
    cJSON *root = NULL;
    cJSON *params = NULL;
    cJSON *item = NULL;
    cJSON *id_item = NULL;
    char message_id[24];
    OneNetThresholdUpdate_t update;
    uint8_t handled_any = 0U;
    uint8_t success = 1U;

    if (payload == NULL)
    {
        return;
    }

    memset(message_id, 0, sizeof(message_id));
    strncpy(message_id, "0", sizeof(message_id) - 1U);
    memset(&update, 0, sizeof(update));

    root = cJSON_Parse(payload);
    if (root == NULL)
    {
        OneNet_RequestPropertySetReply(message_id, ONENET_REPLY_CODE_BAD_REQUEST);
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
            if (item->string == NULL)
            {
                success = 0U;
                continue;
            }

            handled_any = 1U;
            if (OneNet_ApplyThresholdParam(&update, item->string, item) == 0U)
            {
                success = 0U;
            }
        }
    }

    if ((handled_any == 0U) || (success == 0U))
    {
        OneNet_RequestPropertySetReply(message_id, ONENET_REPLY_CODE_BAD_REQUEST);
    }
    else
    {
        if (onenet_threshold_callback != NULL)
        {
            onenet_threshold_callback(&update);
        }
        OneNet_RequestPropertySetReply(message_id, ONENET_REPLY_CODE_OK);
    }

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

static void OneNet_QueueRxFrame(const uint8_t *data, uint16_t len)
{
    OneNetRxFrame_t *slot = NULL;

    if ((data == NULL) || (len == 0U) || (len > ESP_DATA_BUF_SIZE))
    {
        return;
    }

    if (onenet_rx_count >= ONENET_RX_QUEUE_DEPTH)
    {
        return;
    }

    slot = &onenet_rx_queue[onenet_rx_head];
    memcpy(slot->data, data, len);
    slot->len = len;
    onenet_rx_head = (uint8_t)((onenet_rx_head + 1U) % ONENET_RX_QUEUE_DEPTH);
    onenet_rx_count++;
}

static void OneNet_ProcessRxQueue(void)
{
    OneNetRxFrame_t *slot = NULL;

    if (onenet_rx_count == 0U)
    {
        return;
    }

    slot = &onenet_rx_queue[onenet_rx_tail];
    OneNet_HandleMqttFrame(slot->data, slot->len);
    slot->len = 0U;
    onenet_rx_tail = (uint8_t)((onenet_rx_tail + 1U) % ONENET_RX_QUEUE_DEPTH);
    onenet_rx_count--;
}

static void OneNet_RxCallback(ESP_DataPacket_t *packet)
{
    if ((packet == NULL) || (packet->len == 0U))
    {
        return;
    }

    OneNet_QueueRxFrame(packet->data, packet->len);
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
    onenet_pending_set_reply_attempts = 0U;
    memset(onenet_pending_set_reply_id, 0, sizeof(onenet_pending_set_reply_id));

    memset(&onenet_telemetry_cache, 0, sizeof(onenet_telemetry_cache));
    onenet_telemetry_valid = 0U;
    onenet_report_interval_ms = ONENET_DEFAULT_REPORT_MS;
    onenet_last_report_tick = HAL_GetTick();
    memset(onenet_rx_queue, 0, sizeof(onenet_rx_queue));
    onenet_rx_head = 0U;
    onenet_rx_tail = 0U;
    onenet_rx_count = 0U;

    ESP_RegisterCallback(OneNet_RxCallback);

    if (ESP_Init() != ESP_OK)
    {
        return 0U;
    }

    return 1U;
}

void OneNet_RegisterThresholdCallback(OneNet_ThresholdCallback_t cb)
{
    onenet_threshold_callback = cb;
}

void OneNet_Process(void)
{
    uint32_t now = HAL_GetTick();

    ESP_Process();
    OneNet_ProcessRxQueue();

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
        uint32_t wait_ms = (onenet_pending_set_reply_attempts == 0U) ?
            ONENET_SET_REPLY_DELAY_MS :
            ONENET_SET_REPLY_RETRY_MS;
        uint8_t send_result = ONENET_SEND_FAIL;

        if ((now - onenet_pending_set_reply_tick) < wait_ms)
        {
            goto publish_telemetry;
        }

        onenet_pending_set_reply_attempts++;
        send_result = OneNet_SendPropertySetReply(onenet_pending_set_reply_id, onenet_pending_set_reply_code);
        if (send_result == ONENET_SEND_OK)
        {
            OneNet_ClearPropertySetReply();
        }
        else if (send_result == ONENET_SEND_BUSY)
        {
            if (onenet_pending_set_reply_attempts > 0U)
            {
                onenet_pending_set_reply_attempts--;
            }
            onenet_pending_set_reply_tick = now;
        }
        else if (onenet_pending_set_reply_attempts >= ONENET_SET_REPLY_MAX_ATTEMPTS)
        {
            OneNet_ClearPropertySetReply();
        }
        else
        {
            onenet_pending_set_reply_tick = now;
        }
    }

publish_telemetry:
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
