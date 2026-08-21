#include "onenet.h"

#include "HARDWARE/ESP_01S/ESP_01S.h"
#include "MqttKit.h"
#include "cJSON.h"
#include "onenet_config.h"
#include "stm32f4xx_hal.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ONENET_TOPIC_PREFIX "$sys/" ONENET_PRODUCT_ID "/" ONENET_DEVICE_NAME "/thing/"
#define ONENET_PROPERTY_POST_TOPIC ONENET_TOPIC_PREFIX "property/post"
#define ONENET_PROPERTY_POST_REPLY_TOPIC ONENET_TOPIC_PREFIX "property/post/reply"
#define ONENET_PROPERTY_SET_TOPIC ONENET_TOPIC_PREFIX "property/set"
#define ONENET_PROPERTY_SET_REPLY_TOPIC ONENET_TOPIC_PREFIX "property/set_reply"
#define ONENET_PROPERTY_GET_TOPIC ONENET_TOPIC_PREFIX "property/get"
#define ONENET_PROPERTY_GET_REPLY_TOPIC ONENET_TOPIC_PREFIX "property/get_reply"
#define ONENET_EVENT_POST_TOPIC ONENET_TOPIC_PREFIX "event/post"
#define ONENET_EVENT_POST_REPLY_TOPIC ONENET_TOPIC_PREFIX "event/post/reply"
#define ONENET_SERVICE_TOPIC_PREFIX ONENET_TOPIC_PREFIX "service/"
#define ONENET_SERVICE_TOPIC_SUFFIX "/invoke"

#define ONENET_KEEPALIVE_SECONDS 60U
#define ONENET_CONNECT_RETRY_MS 5000U
#define ONENET_PING_INTERVAL_MS 30000U
#define ONENET_PAYLOAD_BUF_SIZE 2048U
#define ONENET_TOPIC_BUF_SIZE 128U
#define ONENET_RX_STREAM_SIZE 4096U
#define ONENET_TX_QUEUE_DEPTH 8U
#define ONENET_REPLY_CODE_OK 200U
#define ONENET_REPLY_CODE_BAD_REQUEST 400U

typedef struct
{
    uint8_t data[ESP_TX_BUF_SIZE];
    uint16_t len;
} OneNetTxFrame_t;

static uint8_t onenet_mqtt_connected = 0U;
static uint8_t onenet_subscribed = 0U;
static uint8_t onenet_immediate_report = 0U;
static uint32_t onenet_last_connect_attempt = 0U;
static uint32_t onenet_last_subscribe_attempt = 0U;
static uint32_t onenet_last_tx_tick = 0U;
static uint32_t onenet_message_id = 1U;
static uint16_t onenet_packet_id = 1U;
static uint8_t onenet_rx_stream[ONENET_RX_STREAM_SIZE];
static uint16_t onenet_rx_stream_len = 0U;
static OneNetTxFrame_t onenet_tx_queue[ONENET_TX_QUEUE_DEPTH];
static uint8_t onenet_tx_head = 0U;
static uint8_t onenet_tx_tail = 0U;
static uint8_t onenet_tx_count = 0U;
static OneNet_PropertySetCallback_t onenet_property_callback = NULL;
static OneNet_ServiceCallback_t onenet_service_callback = NULL;
static OneNetLockerStatus_t onenet_last_status;
static char onenet_last_firmware_version[64];
static char onenet_last_device_time[32];
static char onenet_last_error_message[128];
static uint8_t onenet_last_status_valid = 0U;

static uint16_t OneNet_NextPacketId(void);
static uint32_t OneNet_NextMessageId(void);
static uint8_t OneNet_Append(char *buffer, uint16_t size, uint16_t *used, const char *fmt, ...);
static uint8_t OneNet_SendPacket(MQTT_PACKET_STRUCTURE *packet);
static uint8_t OneNet_SendConnect(void);
static uint8_t OneNet_SendSubscribe(void);
static uint8_t OneNet_SendPing(void);
static uint8_t OneNet_PublishRaw(const char *topic, const char *payload, uint8_t qos);
static uint8_t OneNet_SendPublishResponse(uint8_t qos, uint16_t packet_id);
static uint8_t OneNet_SendPropertySetReply(const char *request_id, uint16_t code, const char *message);
static uint8_t OneNet_SendPropertyGetReply(const char *request_id, uint16_t code, const char *message);
static uint8_t OneNet_AppendStatusData(char *buffer, uint16_t size, uint16_t *used, const OneNetLockerStatus_t *status);
static uint8_t OneNet_ReadIntValue(const cJSON *item, int32_t min_value, int32_t max_value, int32_t *out_value);
static uint8_t OneNet_ReadBoolValue(const cJSON *item, uint8_t *out_value);
static uint8_t OneNet_ReadStringValue(const cJSON *item, char *out, uint16_t out_len);
static const cJSON *OneNet_GetValueNode(const cJSON *item);
static const cJSON *OneNet_GetParams(cJSON *root);
static void OneNet_ReadMessageId(cJSON *root, char *out, uint16_t out_len);
static uint8_t OneNet_ApplyPropertyParam(OneNetPropertySet_t *set, const char *name, const cJSON *item);
static void OneNet_HandlePropertySetPayload(const char *payload);
static void OneNet_HandlePropertyGetPayload(const char *payload);
static OneNetServiceType_t OneNet_ServiceTypeFromIdentifier(const char *identifier);
static uint8_t OneNet_ParseServiceIdentifier(const char *topic, char *out, uint16_t out_len);
static void OneNet_HandleServicePayload(const char *topic, const char *payload);
static int32_t OneNet_ParseMqttPacket(const uint8_t *data, uint16_t len, uint16_t *packet_len);
static void OneNet_ResetRxStream(void);
static void OneNet_AppendRxStream(const uint8_t *data, uint16_t len);
static void OneNet_ProcessRxStream(void);
static void OneNet_ResetTxQueue(void);
static uint8_t OneNet_QueueTxFrame(const uint8_t *data, uint16_t len);
static void OneNet_ProcessTxQueue(void);
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

static uint32_t OneNet_NextMessageId(void)
{
    uint32_t current = onenet_message_id;

    onenet_message_id++;
    if (onenet_message_id == 0U)
    {
        onenet_message_id = 1U;
    }

    return current;
}

static uint8_t OneNet_Append(char *buffer, uint16_t size, uint16_t *used, const char *fmt, ...)
{
    va_list args;
    int written = 0;

    if ((buffer == NULL) || (used == NULL) || (fmt == NULL) || (*used >= size))
    {
        return 0U;
    }

    va_start(args, fmt);
    written = vsnprintf(&buffer[*used], (uint16_t)(size - *used), fmt, args);
    va_end(args);

    if ((written < 0) || ((uint16_t)written >= (uint16_t)(size - *used)))
    {
        buffer[size - 1U] = '\0';
        return 0U;
    }

    *used = (uint16_t)(*used + (uint16_t)written);
    return 1U;
}

static void OneNet_ResetRxStream(void)
{
    memset(onenet_rx_stream, 0, sizeof(onenet_rx_stream));
    onenet_rx_stream_len = 0U;
}

static int32_t OneNet_ParseMqttPacket(const uint8_t *data, uint16_t len, uint16_t *packet_len)
{
    uint32_t remain_len = 0U;
    uint32_t multiplier = 1U;
    uint32_t total_len = 0U;
    uint8_t packet_type = 0U;
    uint8_t i = 0U;

    if ((data == NULL) || (packet_len == NULL))
    {
        return -2;
    }

    *packet_len = 0U;
    if (len < 2U)
    {
        return -1;
    }

    packet_type = (uint8_t)(data[0] >> 4);
    if ((packet_type < 1U) || (packet_type > 14U))
    {
        return -2;
    }

    for (i = 0U; i < 4U; i++)
    {
        uint8_t encoded = 0U;

        if ((uint16_t)(1U + i) >= len)
        {
            return -1;
        }

        encoded = data[1U + i];
        remain_len += (uint32_t)(encoded & 0x7FU) * multiplier;
        if ((encoded & 0x80U) == 0U)
        {
            total_len = 1U + (uint32_t)i + 1U + remain_len;
            if (total_len > ONENET_RX_STREAM_SIZE)
            {
                return -2;
            }
            if (total_len > (uint32_t)len)
            {
                return -1;
            }

            *packet_len = (uint16_t)total_len;
            return 0;
        }

        if (i == 3U)
        {
            return -2;
        }

        multiplier <<= 7;
    }

    return -2;
}

static void OneNet_ProcessRxStream(void)
{
    uint16_t frame_len = 0U;
    int32_t status = 0;

    while (onenet_rx_stream_len > 0U)
    {
        status = OneNet_ParseMqttPacket(onenet_rx_stream, onenet_rx_stream_len, &frame_len);
        if (status == -1)
        {
            return;
        }

        if ((status != 0) || (frame_len == 0U))
        {
            if (onenet_rx_stream_len > 1U)
            {
                memmove(onenet_rx_stream, onenet_rx_stream + 1U, (size_t)(onenet_rx_stream_len - 1U));
            }
            onenet_rx_stream_len = (uint16_t)((onenet_rx_stream_len > 0U) ? (onenet_rx_stream_len - 1U) : 0U);
            continue;
        }

        OneNet_HandleMqttFrame(onenet_rx_stream, frame_len);

        if (frame_len < onenet_rx_stream_len)
        {
            memmove(onenet_rx_stream, onenet_rx_stream + frame_len, (size_t)(onenet_rx_stream_len - frame_len));
        }
        onenet_rx_stream_len = (uint16_t)(onenet_rx_stream_len - frame_len);
    }
}

static void OneNet_AppendRxStream(const uint8_t *data, uint16_t len)
{
    uint16_t free_space = 0U;

    if ((data == NULL) || (len == 0U))
    {
        return;
    }

    if (len > ONENET_RX_STREAM_SIZE)
    {
        OneNet_ResetRxStream();
        return;
    }

    free_space = (uint16_t)(ONENET_RX_STREAM_SIZE - onenet_rx_stream_len);
    if (len > free_space)
    {
        OneNet_ProcessRxStream();
        free_space = (uint16_t)(ONENET_RX_STREAM_SIZE - onenet_rx_stream_len);
        if (len > free_space)
        {
            OneNet_ResetRxStream();
        }
    }

    if (len <= (uint16_t)(ONENET_RX_STREAM_SIZE - onenet_rx_stream_len))
    {
        memcpy(&onenet_rx_stream[onenet_rx_stream_len], data, len);
        onenet_rx_stream_len = (uint16_t)(onenet_rx_stream_len + len);
        OneNet_ProcessRxStream();
    }
}

static void OneNet_ResetTxQueue(void)
{
    memset(onenet_tx_queue, 0, sizeof(onenet_tx_queue));
    onenet_tx_head = 0U;
    onenet_tx_tail = 0U;
    onenet_tx_count = 0U;
}

static uint8_t OneNet_QueueTxFrame(const uint8_t *data, uint16_t len)
{
    OneNetTxFrame_t *slot = NULL;

    if ((data == NULL) || (len == 0U) || (len > ESP_TX_BUF_SIZE) || (onenet_tx_count >= ONENET_TX_QUEUE_DEPTH))
    {
        return 0U;
    }

    slot = &onenet_tx_queue[onenet_tx_head];
    memcpy(slot->data, data, len);
    slot->len = len;
    onenet_tx_head = (uint8_t)((onenet_tx_head + 1U) % ONENET_TX_QUEUE_DEPTH);
    onenet_tx_count++;
    return 1U;
}

static void OneNet_ProcessTxQueue(void)
{
    OneNetTxFrame_t *slot = NULL;
    ESP_Status_t status = ESP_ERR_SEND_FAIL;

    if (onenet_tx_count == 0U)
    {
        return;
    }

    if (ESP_IsConnected() == 0U)
    {
        return;
    }

    slot = &onenet_tx_queue[onenet_tx_tail];
    status = ESP_SendDataAsync(0U, slot->data, slot->len);
    if (status == ESP_OK)
    {
        onenet_last_tx_tick = HAL_GetTick();
        slot->len = 0U;
        onenet_tx_tail = (uint8_t)((onenet_tx_tail + 1U) % ONENET_TX_QUEUE_DEPTH);
        onenet_tx_count--;
        return;
    }

    if (status != ESP_ERR_BUSY)
    {
        onenet_mqtt_connected = 0U;
        onenet_subscribed = 0U;
        OneNet_ResetTxQueue();
    }
}

static uint8_t OneNet_SendPacket(MQTT_PACKET_STRUCTURE *packet)
{
    ESP_Status_t status = ESP_ERR_SEND_FAIL;
    uint8_t queued = 0U;

    if (packet == NULL)
    {
        return 0U;
    }

    if ((packet->_data == NULL) || (packet->_len == 0U) || (packet->_len > ESP_TX_BUF_SIZE))
    {
        MQTT_DeleteBuffer(packet);
        return 0U;
    }

    if (onenet_tx_count != 0U)
    {
        if (ESP_IsConnected() != 0U)
        {
            queued = OneNet_QueueTxFrame(packet->_data, (uint16_t)packet->_len);
        }
        MQTT_DeleteBuffer(packet);
        return queued;
    }

    status = ESP_SendDataAsync(0U, packet->_data, (uint16_t)packet->_len);
    if (status == ESP_OK)
    {
        onenet_last_tx_tick = HAL_GetTick();
        MQTT_DeleteBuffer(packet);
        return 1U;
    }

    if (status == ESP_ERR_BUSY)
    {
        if (ESP_IsConnected() != 0U)
        {
            queued = OneNet_QueueTxFrame(packet->_data, (uint16_t)packet->_len);
        }
        MQTT_DeleteBuffer(packet);
        return queued;
    }

    MQTT_DeleteBuffer(packet);
    onenet_mqtt_connected = 0U;
    onenet_subscribed = 0U;
    OneNet_ResetTxQueue();
    return 0U;
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
    const int8 *topics[] = {
        ONENET_PROPERTY_POST_REPLY_TOPIC,
        ONENET_PROPERTY_SET_TOPIC,
        ONENET_PROPERTY_GET_TOPIC,
        ONENET_EVENT_POST_REPLY_TOPIC,
        ONENET_SERVICE_TOPIC_PREFIX "query_status" ONENET_SERVICE_TOPIC_SUFFIX,
        ONENET_SERVICE_TOPIC_PREFIX "open_locker" ONENET_SERVICE_TOPIC_SUFFIX,
        ONENET_SERVICE_TOPIC_PREFIX "clear_alarm" ONENET_SERVICE_TOPIC_SUFFIX,
        ONENET_SERVICE_TOPIC_PREFIX "export_records" ONENET_SERVICE_TOPIC_SUFFIX,
        ONENET_SERVICE_TOPIC_PREFIX "clear_all_lockers" ONENET_SERVICE_TOPIC_SUFFIX,
        ONENET_SERVICE_TOPIC_PREFIX "set_device_time" ONENET_SERVICE_TOPIC_SUFFIX,
        ONENET_SERVICE_TOPIC_PREFIX "reboot_device" ONENET_SERVICE_TOPIC_SUFFIX};

    onenet_last_subscribe_attempt = HAL_GetTick();

    if (MQTT_PacketSubscribe(MQTT_SUBSCRIBE_ID,
                             MQTT_QOS_LEVEL0,
                             topics,
                             (uint8_t)(sizeof(topics) / sizeof(topics[0])),
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

static uint8_t OneNet_PublishRaw(const char *topic, const char *payload, uint8_t qos)
{
    MQTT_PACKET_STRUCTURE packet = {0};
    uint32_t payload_len = 0U;

    if ((topic == NULL) || (payload == NULL) || (OneNet_IsConnected() == 0U))
    {
        return 0U;
    }

    payload_len = (uint32_t)strlen(payload);
    if (MQTT_PacketPublish(OneNet_NextPacketId(),
                           topic,
                           payload,
                           payload_len,
                           (qos == 0U) ? MQTT_QOS_LEVEL0 : MQTT_QOS_LEVEL1,
                           0,
                           0,
                           &packet) != 0U)
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

static uint8_t OneNet_SendPropertySetReply(const char *request_id, uint16_t code, const char *message)
{
    char payload[160];
    int written = 0;

    written = snprintf(payload,
                       sizeof(payload),
                       "{\"id\":\"%s\",\"code\":%u,\"msg\":\"%s\"}",
                       (request_id != NULL) ? request_id : "0",
                       (unsigned int)code,
                       (message != NULL) ? message : "");
    if ((written < 0) || ((uint32_t)written >= sizeof(payload)))
    {
        return 0U;
    }

    return OneNet_PublishRaw(ONENET_PROPERTY_SET_REPLY_TOPIC, payload, 1U);
}

static uint8_t OneNet_AppendStatusData(char *buffer, uint16_t size, uint16_t *used, const OneNetLockerStatus_t *status)
{
    uint8_t i = 0U;
    const OneNetLockerRecord_t *rec = NULL;

    if ((buffer == NULL) || (used == NULL) || (status == NULL))
    {
        return 0U;
    }

    rec = &status->last_record;
    if (OneNet_Append(buffer, size, used, "{") == 0U)
    {
        return 0U;
    }

    (void)OneNet_Append(buffer, size, used, "\"protocol_version\":%u,", (unsigned int)status->protocol_version);
    (void)OneNet_Append(buffer, size, used, "\"firmware_version\":\"%s\",", (status->firmware_version != NULL) ? status->firmware_version : "");
    (void)OneNet_Append(buffer, size, used, "\"device_time\":\"%s\",", (status->device_time != NULL) ? status->device_time : "");
    (void)OneNet_Append(buffer, size, used, "\"device_state\":%u,", (unsigned int)status->device_state);
    (void)OneNet_Append(buffer, size, used, "\"cloud_connected\":%s,", (status->cloud_connected != 0U) ? "true" : "false");
    (void)OneNet_Append(buffer, size, used, "\"wifi_rssi\":%ld,", (long)status->wifi_rssi);
    (void)OneNet_Append(buffer, size, used, "\"locker_count\":%u,", (unsigned int)status->locker_count);
    (void)OneNet_Append(buffer, size, used, "\"used_count\":%u,", (unsigned int)status->used_count);
    (void)OneNet_Append(buffer, size, used, "\"free_count\":%u,", (unsigned int)status->free_count);
    (void)OneNet_Append(buffer, size, used, "\"locker_occupancy\":[");
    for (i = 0U; i < ONENET_LOCKER_COUNT; i++)
    {
        (void)OneNet_Append(buffer, size, used, "%u%s",
                            (unsigned int)(status->locker_occupancy[i] != 0U),
                            (i == (ONENET_LOCKER_COUNT - 1U)) ? "]" : ",");
    }
    (void)OneNet_Append(buffer, size, used, ",");
    (void)OneNet_Append(buffer, size, used, "\"locker_bitmap\":%u,", (unsigned int)status->locker_bitmap);
    (void)OneNet_Append(buffer, size, used, "\"active_locker_id\":%u,", (unsigned int)status->active_locker_id);
    (void)OneNet_Append(buffer, size, used, "\"door_open\":%s,", (status->door_open != 0U) ? "true" : "false");
    (void)OneNet_Append(buffer, size, used, "\"door_open_elapsed_sec\":%lu,", (unsigned long)status->door_open_elapsed_sec);
    (void)OneNet_Append(buffer, size, used, "\"alarm_status\":%u,", (unsigned int)status->alarm_status);
    (void)OneNet_Append(buffer, size, used, "\"buzzer_active\":%s,", (status->buzzer_active != 0U) ? "true" : "false");
    (void)OneNet_Append(buffer, size, used, "\"current_operation\":%u,", (unsigned int)status->current_operation);
    (void)OneNet_Append(buffer, size, used, "\"record_count\":%u,", (unsigned int)status->record_count);
    (void)OneNet_Append(buffer, size, used,
                        "\"last_record\":{\"op\":%u,\"locker_id\":%u,\"face_id\":%d,\"result\":%s,\"time\":\"%s\"},",
                        (unsigned int)rec->op,
                        (unsigned int)rec->locker_id,
                        (int)rec->face_id,
                        (rec->result != 0U) ? "true" : "false",
                        rec->timestamp);
    (void)OneNet_Append(buffer, size, used, "\"remote_control_enabled\":%s,", (status->remote_control_enabled != 0U) ? "true" : "false");
    (void)OneNet_Append(buffer, size, used, "\"door_timeout_sec\":%u,", (unsigned int)status->door_timeout_sec);
    (void)OneNet_Append(buffer, size, used, "\"status_report_interval_sec\":%u,", (unsigned int)status->status_report_interval_sec);
    (void)OneNet_Append(buffer, size, used, "\"maintenance_mode\":%s,", (status->maintenance_mode != 0U) ? "true" : "false");
    (void)OneNet_Append(buffer, size, used, "\"last_error_message\":\"%s\"}", (status->last_error_message != NULL) ? status->last_error_message : "");

    return 1U;
}

static uint8_t OneNet_SendPropertyGetReply(const char *request_id, uint16_t code, const char *message)
{
    char payload[ONENET_PAYLOAD_BUF_SIZE];
    uint16_t used = 0U;

    if (OneNet_Append(payload, sizeof(payload), &used,
                      "{\"id\":\"%s\",\"code\":%u,\"msg\":\"%s\",\"data\":",
                      (request_id != NULL) ? request_id : "0",
                      (unsigned int)code,
                      (message != NULL) ? message : "") == 0U)
    {
        return 0U;
    }

    if ((onenet_last_status_valid != 0U) && (OneNet_AppendStatusData(payload, sizeof(payload), &used, &onenet_last_status) == 0U))
    {
        return 0U;
    }
    else if (onenet_last_status_valid == 0U)
    {
        if (OneNet_Append(payload, sizeof(payload), &used, "{}") == 0U)
        {
            return 0U;
        }
    }

    if (OneNet_Append(payload, sizeof(payload), &used, "}") == 0U)
    {
        return 0U;
    }

    return OneNet_PublishRaw(ONENET_PROPERTY_GET_REPLY_TOPIC, payload, 1U);
}

static const cJSON *OneNet_GetValueNode(const cJSON *item)
{
    cJSON *value_node = NULL;

    if (item == NULL)
    {
        return NULL;
    }

    if ((item->type & 0xFF) == cJSON_Object)
    {
        value_node = cJSON_GetObjectItem((cJSON *)item, "value");
        if (value_node != NULL)
        {
            return value_node;
        }
    }

    return item;
}

static uint8_t OneNet_ReadIntValue(const cJSON *item, int32_t min_value, int32_t max_value, int32_t *out_value)
{
    const cJSON *value_node = OneNet_GetValueNode(item);
    int32_t value = 0;

    if ((value_node == NULL) || (out_value == NULL))
    {
        return 0U;
    }

    if ((value_node->type & 0xFF) == cJSON_Number)
    {
        value = value_node->valueint;
    }
    else if (((value_node->type & 0xFF) == cJSON_String) && (value_node->valuestring != NULL))
    {
        long parsed = 0;
        if (sscanf(value_node->valuestring, "%ld", &parsed) != 1)
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

static uint8_t OneNet_ReadBoolValue(const cJSON *item, uint8_t *out_value)
{
    const cJSON *value_node = OneNet_GetValueNode(item);

    if ((value_node == NULL) || (out_value == NULL))
    {
        return 0U;
    }

    if ((value_node->type & 0xFF) == cJSON_True)
    {
        *out_value = 1U;
        return 1U;
    }

    if ((value_node->type & 0xFF) == cJSON_False)
    {
        *out_value = 0U;
        return 1U;
    }

    if ((value_node->type & 0xFF) == cJSON_Number)
    {
        *out_value = (value_node->valueint != 0) ? 1U : 0U;
        return 1U;
    }

    return 0U;
}

static uint8_t OneNet_ReadStringValue(const cJSON *item, char *out, uint16_t out_len)
{
    const cJSON *value_node = OneNet_GetValueNode(item);

    if ((value_node == NULL) || (out == NULL) || (out_len == 0U))
    {
        return 0U;
    }

    if (((value_node->type & 0xFF) != cJSON_String) || (value_node->valuestring == NULL))
    {
        return 0U;
    }

    strncpy(out, value_node->valuestring, out_len - 1U);
    out[out_len - 1U] = '\0';
    return 1U;
}

static const cJSON *OneNet_GetParams(cJSON *root)
{
    cJSON *params = NULL;

    if ((root == NULL) || ((root->type & 0xFF) != cJSON_Object))
    {
        return NULL;
    }

    params = cJSON_GetObjectItem(root, "params");
    if ((params == NULL) || ((params->type & 0xFF) != cJSON_Object))
    {
        return NULL;
    }

    return params;
}

static void OneNet_ReadMessageId(cJSON *root, char *out, uint16_t out_len)
{
    cJSON *id_item = NULL;

    if ((out == NULL) || (out_len == 0U))
    {
        return;
    }

    strncpy(out, "0", out_len - 1U);
    out[out_len - 1U] = '\0';

    if (root == NULL)
    {
        return;
    }

    id_item = cJSON_GetObjectItem(root, "id");
    if (id_item == NULL)
    {
        return;
    }

    if (((id_item->type & 0xFF) == cJSON_String) && (id_item->valuestring != NULL))
    {
        strncpy(out, id_item->valuestring, out_len - 1U);
        out[out_len - 1U] = '\0';
    }
    else if ((id_item->type & 0xFF) == cJSON_Number)
    {
        snprintf(out, out_len, "%d", id_item->valueint);
    }
}

static uint8_t OneNet_ApplyPropertyParam(OneNetPropertySet_t *set, const char *name, const cJSON *item)
{
    int32_t int_value = 0;
    uint8_t bool_value = 0U;

    if ((set == NULL) || (name == NULL) || (item == NULL))
    {
        return 0U;
    }

    if (strcmp(name, "door_timeout_sec") == 0)
    {
        if (OneNet_ReadIntValue(item, 10, 300, &int_value) == 0U)
        {
            return 0U;
        }
        set->door_timeout_sec = (uint16_t)int_value;
        set->has_door_timeout_sec = 1U;
        return 1U;
    }

    if (strcmp(name, "status_report_interval_sec") == 0)
    {
        if (OneNet_ReadIntValue(item, 5, 3600, &int_value) == 0U)
        {
            return 0U;
        }
        set->status_report_interval_sec = (uint16_t)int_value;
        set->has_status_report_interval_sec = 1U;
        return 1U;
    }

    if (strcmp(name, "remote_control_enabled") == 0)
    {
        if (OneNet_ReadBoolValue(item, &bool_value) == 0U)
        {
            return 0U;
        }
        set->remote_control_enabled = bool_value;
        set->has_remote_control_enabled = 1U;
        return 1U;
    }

    if (strcmp(name, "maintenance_mode") == 0)
    {
        if (OneNet_ReadBoolValue(item, &bool_value) == 0U)
        {
            return 0U;
        }
        set->maintenance_mode = bool_value;
        set->has_maintenance_mode = 1U;
        return 1U;
    }

    return 0U;
}

static void OneNet_HandlePropertySetPayload(const char *payload)
{
    cJSON *root = NULL;
    const cJSON *params = NULL;
    cJSON *item = NULL;
    OneNetPropertySet_t set;
    char request_id[32];
    uint8_t handled_any = 0U;
    uint8_t success = 1U;

    memset(&set, 0, sizeof(set));
    memset(request_id, 0, sizeof(request_id));

    root = cJSON_Parse(payload);
    if (root == NULL)
    {
        (void)OneNet_SendPropertySetReply("0", ONENET_REPLY_CODE_BAD_REQUEST, "bad json");
        return;
    }

    OneNet_ReadMessageId(root, request_id, sizeof(request_id));
    params = OneNet_GetParams(root);
    if (params == NULL)
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
            if (OneNet_ApplyPropertyParam(&set, item->string, item) == 0U)
            {
                success = 0U;
            }
        }
    }

    if ((handled_any != 0U) && (success != 0U))
    {
        if (onenet_property_callback != NULL)
        {
            onenet_property_callback(&set, request_id);
        }
        (void)OneNet_SendPropertySetReply(request_id, ONENET_REPLY_CODE_OK, "success");
        OneNet_RequestImmediateReport();
    }
    else
    {
        (void)OneNet_SendPropertySetReply(request_id, ONENET_REPLY_CODE_BAD_REQUEST, "bad request");
    }

    cJSON_Delete(root);
}

static void OneNet_HandlePropertyGetPayload(const char *payload)
{
    cJSON *root = NULL;
    char request_id[32];

    memset(request_id, 0, sizeof(request_id));
    if (payload == NULL)
    {
        (void)OneNet_SendPropertyGetReply("0", ONENET_REPLY_CODE_BAD_REQUEST, "bad json");
        return;
    }

    root = cJSON_Parse(payload);
    if (root == NULL)
    {
        (void)OneNet_SendPropertyGetReply("0", ONENET_REPLY_CODE_BAD_REQUEST, "bad json");
        return;
    }

    OneNet_ReadMessageId(root, request_id, sizeof(request_id));
    cJSON_Delete(root);
    (void)OneNet_SendPropertyGetReply(request_id, ONENET_REPLY_CODE_OK, "success");
    OneNet_RequestImmediateReport();
}

static OneNetServiceType_t OneNet_ServiceTypeFromIdentifier(const char *identifier)
{
    if (identifier == NULL)
    {
        return ONENET_SERVICE_UNKNOWN;
    }
    if (strcmp(identifier, "query_status") == 0)
    {
        return ONENET_SERVICE_QUERY_STATUS;
    }
    if (strcmp(identifier, "open_locker") == 0)
    {
        return ONENET_SERVICE_OPEN_LOCKER;
    }
    if (strcmp(identifier, "clear_alarm") == 0)
    {
        return ONENET_SERVICE_CLEAR_ALARM;
    }
    if (strcmp(identifier, "export_records") == 0)
    {
        return ONENET_SERVICE_EXPORT_RECORDS;
    }
    if (strcmp(identifier, "clear_all_lockers") == 0)
    {
        return ONENET_SERVICE_CLEAR_ALL_LOCKERS;
    }
    if (strcmp(identifier, "set_device_time") == 0)
    {
        return ONENET_SERVICE_SET_DEVICE_TIME;
    }
    if (strcmp(identifier, "reboot_device") == 0)
    {
        return ONENET_SERVICE_REBOOT_DEVICE;
    }
    return ONENET_SERVICE_UNKNOWN;
}

static uint8_t OneNet_ParseServiceIdentifier(const char *topic, char *out, uint16_t out_len)
{
    const char *start = NULL;
    const char *end = NULL;
    uint16_t len = 0U;

    if ((topic == NULL) || (out == NULL) || (out_len == 0U))
    {
        return 0U;
    }

    if (strncmp(topic, ONENET_SERVICE_TOPIC_PREFIX, strlen(ONENET_SERVICE_TOPIC_PREFIX)) != 0)
    {
        return 0U;
    }

    start = topic + strlen(ONENET_SERVICE_TOPIC_PREFIX);
    end = strstr(start, ONENET_SERVICE_TOPIC_SUFFIX);
    if ((end == NULL) || (end == start) || (end[strlen(ONENET_SERVICE_TOPIC_SUFFIX)] != '\0'))
    {
        return 0U;
    }

    len = (uint16_t)(end - start);
    if (len >= out_len)
    {
        len = (uint16_t)(out_len - 1U);
    }
    memcpy(out, start, len);
    out[len] = '\0';
    return 1U;
}

static void OneNet_HandleServicePayload(const char *topic, const char *payload)
{
    cJSON *root = NULL;
    const cJSON *params = NULL;
    cJSON *item = NULL;
    OneNetServiceRequest_t req;

    memset(&req, 0, sizeof(req));
    if (OneNet_ParseServiceIdentifier(topic, req.identifier, sizeof(req.identifier)) == 0U)
    {
        return;
    }
    req.type = OneNet_ServiceTypeFromIdentifier(req.identifier);
    root = cJSON_Parse(payload);
    if (root == NULL)
    {
        (void)OneNet_ReplyService(req.identifier, "0", ONENET_REPLY_CODE_BAD_REQUEST, "bad json", "{}");
        return;
    }

    OneNet_ReadMessageId(root, req.request_id, sizeof(req.request_id));
    params = OneNet_GetParams(root);
    if (params != NULL)
    {
        item = cJSON_GetObjectItem((cJSON *)params, "locker_id");
        if (item != NULL)
        {
            (void)OneNet_ReadIntValue(item, 1, 16, &req.locker_id);
        }
        item = cJSON_GetObjectItem((cJSON *)params, "reason");
        if (item != NULL)
        {
            (void)OneNet_ReadIntValue(item, 1, 4, &req.reason);
        }
        item = cJSON_GetObjectItem((cJSON *)params, "limit");
        if (item != NULL)
        {
            (void)OneNet_ReadIntValue(item, 1, 64, &req.limit);
        }
        item = cJSON_GetObjectItem((cJSON *)params, "operator_id");
        if (item != NULL)
        {
            (void)OneNet_ReadStringValue(item, req.operator_id, sizeof(req.operator_id));
        }
        item = cJSON_GetObjectItem((cJSON *)params, "confirm_code");
        if (item != NULL)
        {
            (void)OneNet_ReadStringValue(item, req.confirm_code, sizeof(req.confirm_code));
        }
        item = cJSON_GetObjectItem((cJSON *)params, "time");
        if (item != NULL)
        {
            (void)OneNet_ReadStringValue(item, req.time, sizeof(req.time));
        }
    }

    if ((req.type == ONENET_SERVICE_UNKNOWN) || (onenet_service_callback == NULL))
    {
        (void)OneNet_ReplyService(req.identifier, req.request_id, ONENET_REPLY_CODE_BAD_REQUEST, "unsupported service", "{}");
    }
    else
    {
        onenet_service_callback(&req);
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
            OneNet_RequestImmediateReport();
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
        uint16_t topic_len = 0U;
        uint16_t payload_len = 0U;
        uint16_t packet_id = 0U;
        uint8_t qos = 0U;

        if (MQTT_UnPacketPublish(data, &topic, &topic_len, &payload, &payload_len, &qos, &packet_id) == 0U)
        {
            (void)OneNet_SendPublishResponse(qos, packet_id);
            if ((topic != NULL) && (payload != NULL))
            {
                if (strcmp((const char *)topic, ONENET_PROPERTY_SET_TOPIC) == 0)
                {
                    OneNet_HandlePropertySetPayload((const char *)payload);
                }
                else if (strcmp((const char *)topic, ONENET_PROPERTY_GET_TOPIC) == 0)
                {
                    OneNet_HandlePropertyGetPayload((const char *)payload);
                }
                else
                {
                    char service_id[32];
                    if (OneNet_ParseServiceIdentifier((const char *)topic, service_id, sizeof(service_id)) != 0U)
                    {
                        OneNet_HandleServicePayload((const char *)topic, (const char *)payload);
                    }
                }
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

    OneNet_AppendRxStream(packet->data, packet->len);
}

uint8_t OneNet_Init(void)
{
    onenet_mqtt_connected = 0U;
    onenet_subscribed = 0U;
    onenet_immediate_report = 0U;
    onenet_last_status_valid = 0U;
    memset(&onenet_last_status, 0, sizeof(onenet_last_status));
    memset(onenet_last_firmware_version, 0, sizeof(onenet_last_firmware_version));
    memset(onenet_last_device_time, 0, sizeof(onenet_last_device_time));
    memset(onenet_last_error_message, 0, sizeof(onenet_last_error_message));
    onenet_last_connect_attempt = HAL_GetTick() - ONENET_CONNECT_RETRY_MS;
    onenet_last_subscribe_attempt = HAL_GetTick() - ONENET_CONNECT_RETRY_MS;
    onenet_last_tx_tick = HAL_GetTick();
    onenet_message_id = 1U;
    onenet_packet_id = 1U;
    OneNet_ResetRxStream();
    OneNet_ResetTxQueue();

    ESP_RegisterCallback(OneNet_RxCallback);
    if (ESP_Init() != ESP_OK)
    {
        return 0U;
    }

    return 1U;
}

void OneNet_RegisterPropertySetCallback(OneNet_PropertySetCallback_t cb)
{
    onenet_property_callback = cb;
}

void OneNet_RegisterServiceCallback(OneNet_ServiceCallback_t cb)
{
    onenet_service_callback = cb;
}

void OneNet_Process(void)
{
    uint32_t now = HAL_GetTick();

    ESP_Process();
    OneNet_ProcessRxStream();

    if (ESP_IsConnected() == 0U)
    {
        onenet_mqtt_connected = 0U;
        onenet_subscribed = 0U;
        OneNet_ResetRxStream();
        OneNet_ResetTxQueue();
        return;
    }

    OneNet_ProcessTxQueue();
    if (onenet_tx_count != 0U)
    {
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

uint8_t OneNet_IsReady(void)
{
    if ((OneNet_IsConnected() == 0U) || (onenet_subscribed == 0U))
    {
        return 0U;
    }
    return 1U;
}

uint8_t OneNet_PublishProperties(const OneNetLockerStatus_t *status)
{
    char payload[ONENET_PAYLOAD_BUF_SIZE];
    uint16_t used = 0U;
    uint8_t i = 0U;
    const OneNetLockerRecord_t *rec = NULL;

    if (status == NULL)
    {
        return 0U;
    }

    onenet_last_status = *status;
    (void)snprintf(onenet_last_firmware_version,
                   sizeof(onenet_last_firmware_version),
                   "%s",
                   (status->firmware_version != NULL) ? status->firmware_version : "");
    (void)snprintf(onenet_last_device_time,
                   sizeof(onenet_last_device_time),
                   "%s",
                   (status->device_time != NULL) ? status->device_time : "");
    (void)snprintf(onenet_last_error_message,
                   sizeof(onenet_last_error_message),
                   "%s",
                   (status->last_error_message != NULL) ? status->last_error_message : "");
    onenet_last_status.firmware_version = onenet_last_firmware_version;
    onenet_last_status.device_time = onenet_last_device_time;
    onenet_last_status.last_error_message = onenet_last_error_message;
    onenet_last_status_valid = 1U;
    if (OneNet_IsReady() == 0U)
    {
        return 0U;
    }

    rec = &status->last_record;
    memset(payload, 0, sizeof(payload));
    if (OneNet_Append(payload, sizeof(payload), &used,
                      "{\"id\":\"%lu\",\"version\":\"1.0\",\"params\":{",
                      (unsigned long)OneNet_NextMessageId()) == 0U)
    {
        return 0U;
    }

    (void)OneNet_Append(payload, sizeof(payload), &used, "\"protocol_version\":{\"value\":%u},", (unsigned int)status->protocol_version);
    (void)OneNet_Append(payload, sizeof(payload), &used, "\"firmware_version\":{\"value\":\"%s\"},", (status->firmware_version != NULL) ? status->firmware_version : "");
    (void)OneNet_Append(payload, sizeof(payload), &used, "\"device_time\":{\"value\":\"%s\"},", (status->device_time != NULL) ? status->device_time : "");
    (void)OneNet_Append(payload, sizeof(payload), &used, "\"device_state\":{\"value\":%u},", (unsigned int)status->device_state);
    (void)OneNet_Append(payload, sizeof(payload), &used, "\"cloud_connected\":{\"value\":%s},", (status->cloud_connected != 0U) ? "true" : "false");
    (void)OneNet_Append(payload, sizeof(payload), &used, "\"wifi_rssi\":{\"value\":%ld},", (long)status->wifi_rssi);
    (void)OneNet_Append(payload, sizeof(payload), &used, "\"locker_count\":{\"value\":%u},", (unsigned int)status->locker_count);
    (void)OneNet_Append(payload, sizeof(payload), &used, "\"used_count\":{\"value\":%u},", (unsigned int)status->used_count);
    (void)OneNet_Append(payload, sizeof(payload), &used, "\"free_count\":{\"value\":%u},", (unsigned int)status->free_count);
    (void)OneNet_Append(payload, sizeof(payload), &used, "\"locker_occupancy\":{\"value\":[");
    for (i = 0U; i < ONENET_LOCKER_COUNT; i++)
    {
        (void)OneNet_Append(payload, sizeof(payload), &used, "%u%s",
                            (unsigned int)(status->locker_occupancy[i] != 0U),
                            (i == (ONENET_LOCKER_COUNT - 1U)) ? "]" : ",");
    }
    (void)OneNet_Append(payload, sizeof(payload), &used, "},");
    (void)OneNet_Append(payload, sizeof(payload), &used, "\"locker_bitmap\":{\"value\":%u},", (unsigned int)status->locker_bitmap);
    (void)OneNet_Append(payload, sizeof(payload), &used, "\"active_locker_id\":{\"value\":%u},", (unsigned int)status->active_locker_id);
    (void)OneNet_Append(payload, sizeof(payload), &used, "\"door_open\":{\"value\":%s},", (status->door_open != 0U) ? "true" : "false");
    (void)OneNet_Append(payload, sizeof(payload), &used, "\"door_open_elapsed_sec\":{\"value\":%lu},", (unsigned long)status->door_open_elapsed_sec);
    (void)OneNet_Append(payload, sizeof(payload), &used, "\"alarm_status\":{\"value\":%u},", (unsigned int)status->alarm_status);
    (void)OneNet_Append(payload, sizeof(payload), &used, "\"buzzer_active\":{\"value\":%s},", (status->buzzer_active != 0U) ? "true" : "false");
    (void)OneNet_Append(payload, sizeof(payload), &used, "\"current_operation\":{\"value\":%u},", (unsigned int)status->current_operation);
    (void)OneNet_Append(payload, sizeof(payload), &used, "\"record_count\":{\"value\":%u},", (unsigned int)status->record_count);
    (void)OneNet_Append(payload, sizeof(payload), &used,
                        "\"last_record\":{\"value\":{\"op\":%u,\"locker_id\":%u,\"face_id\":%d,\"result\":%s,\"time\":\"%s\"}},",
                        (unsigned int)rec->op,
                        (unsigned int)rec->locker_id,
                        (int)rec->face_id,
                        (rec->result != 0U) ? "true" : "false",
                        rec->timestamp);
    (void)OneNet_Append(payload, sizeof(payload), &used, "\"remote_control_enabled\":{\"value\":%s},", (status->remote_control_enabled != 0U) ? "true" : "false");
    (void)OneNet_Append(payload, sizeof(payload), &used, "\"door_timeout_sec\":{\"value\":%u},", (unsigned int)status->door_timeout_sec);
    (void)OneNet_Append(payload, sizeof(payload), &used, "\"status_report_interval_sec\":{\"value\":%u},", (unsigned int)status->status_report_interval_sec);
    (void)OneNet_Append(payload, sizeof(payload), &used, "\"maintenance_mode\":{\"value\":%s},", (status->maintenance_mode != 0U) ? "true" : "false");
    (void)OneNet_Append(payload, sizeof(payload), &used, "\"last_error_message\":{\"value\":\"%s\"}", (status->last_error_message != NULL) ? status->last_error_message : "");
    if (OneNet_Append(payload, sizeof(payload), &used, "}}") == 0U)
    {
        return 0U;
    }

    return OneNet_PublishRaw(ONENET_PROPERTY_POST_TOPIC, payload, 0U);
}

uint8_t OneNet_PublishEventValue(const char *event_id, const char *value_json)
{
    char payload[ONENET_PAYLOAD_BUF_SIZE];
    int written = 0;

    if ((event_id == NULL) || (value_json == NULL) || (OneNet_IsReady() == 0U))
    {
        return 0U;
    }

    written = snprintf(payload,
                       sizeof(payload),
                       "{\"id\":\"%lu\",\"version\":\"1.0\",\"params\":{\"%s\":{\"value\":{%s}}}}",
                       (unsigned long)OneNet_NextMessageId(),
                       event_id,
                       value_json);
    if ((written < 0) || ((uint32_t)written >= sizeof(payload)))
    {
        return 0U;
    }

    return OneNet_PublishRaw(ONENET_EVENT_POST_TOPIC, payload, 0U);
}

uint8_t OneNet_ReplyService(const char *service_id,
                            const char *request_id,
                            uint16_t platform_code,
                            const char *message,
                            const char *data_json)
{
    char topic[ONENET_TOPIC_BUF_SIZE];
    char payload[512];
    int written = 0;

    if ((service_id == NULL) || (request_id == NULL))
    {
        return 0U;
    }

    written = snprintf(topic,
                       sizeof(topic),
                       ONENET_SERVICE_TOPIC_PREFIX "%s/invoke_reply",
                       service_id);
    if ((written < 0) || ((uint32_t)written >= sizeof(topic)))
    {
        return 0U;
    }

    written = snprintf(payload,
                       sizeof(payload),
                       "{\"id\":\"%s\",\"code\":%u,\"msg\":\"%s\",\"data\":%s}",
                       request_id,
                       (unsigned int)platform_code,
                       (message != NULL) ? message : "",
                       (data_json != NULL) ? data_json : "{}");
    if ((written < 0) || ((uint32_t)written >= sizeof(payload)))
    {
        return 0U;
    }

    return OneNet_PublishRaw(topic, payload, 1U);
}

void OneNet_RequestImmediateReport(void)
{
    onenet_immediate_report = 1U;
}

uint8_t OneNet_ConsumeImmediateReportFlag(void)
{
    uint8_t flag = onenet_immediate_report;
    onenet_immediate_report = 0U;
    return flag;
}

const char *OneNet_ServiceName(OneNetServiceType_t type)
{
    switch (type)
    {
    case ONENET_SERVICE_QUERY_STATUS:
        return "query_status";
    case ONENET_SERVICE_OPEN_LOCKER:
        return "open_locker";
    case ONENET_SERVICE_CLEAR_ALARM:
        return "clear_alarm";
    case ONENET_SERVICE_EXPORT_RECORDS:
        return "export_records";
    case ONENET_SERVICE_CLEAR_ALL_LOCKERS:
        return "clear_all_lockers";
    case ONENET_SERVICE_SET_DEVICE_TIME:
        return "set_device_time";
    case ONENET_SERVICE_REBOOT_DEVICE:
        return "reboot_device";
    default:
        return "unknown";
    }
}
