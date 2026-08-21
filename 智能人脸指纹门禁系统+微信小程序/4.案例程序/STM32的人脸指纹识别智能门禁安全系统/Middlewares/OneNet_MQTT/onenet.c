#include "onenet.h"

#include ".\\Hardware\\ESP_01S\\ESP_01S.h"
#include "MqttKit.h"
#include "cJSON.h"
#include "stm32f1xx_hal.h"

#include <stdio.h>
#include <string.h>

/* ===================== 设备信息（你的 OneNET 云平台） ===================== */
#define ONENET_PRODUCT_ID "1d2Ohi093C"
#define ONENET_DEVICE_NAME "don1ng"
#define ONENET_TOKEN "version=2018-10-31&res=products%2F1d2Ohi093C%2Fdevices%2Fdon1ng&et=1798736400&method=md5&sign=9YxVMH7qYa9IOiXVo16LQA%3D%3D"

/* ===================== MQTT Topic ===================== */
#define ONENET_PROPERTY_POST_TOPIC "$sys/1d2Ohi093C/don1ng/thing/property/post"
#define ONENET_PROPERTY_SET_TOPIC "$sys/1d2Ohi093C/don1ng/thing/property/set"
#define ONENET_PROPERTY_SET_REPLY_TOPIC "$sys/1d2Ohi093C/don1ng/thing/property/set_reply"
#define ONENET_PROPERTY_SET_REPLY_METHOD "thing.property.set.reply"

/* ===================== 配置参数 ===================== */
#define ONENET_KEEPALIVE_SECONDS 60U
#define ONENET_CONNECT_RETRY_MS 5000U
#define ONENET_PING_INTERVAL_MS 30000U
#define ONENET_SET_REPLY_DELAY_MS 200U
#define ONENET_PAYLOAD_BUF_SIZE 300U
#define ONENET_SET_REPLY_BUF_SIZE 256U

/* ===================== 内部状态变量 ===================== */
static uint8_t onenet_mqtt_connected = 0U;
static uint8_t onenet_subscribed = 0U;
static uint32_t onenet_last_connect_attempt = 0U;
static uint32_t onenet_last_subscribe_attempt = 0U;
static uint32_t onenet_last_tx_tick = 0U;
static uint8_t onenet_pending_set_reply = 0U;
static uint16_t onenet_pending_set_reply_code = 0U;
static uint32_t onenet_pending_set_reply_tick = 0U;
static char onenet_pending_set_reply_id[64] = {0};

static uint32_t onenet_message_id = 1U;
static uint16_t onenet_packet_id = 1U;

/* ===================== 用户回调 ===================== */
static OneNet_ServoControlCallback_t onenet_servo_control_callback = NULL;
static OneNet_PasswordSetCallback_t onenet_password_set_callback = NULL;

/* ===================== 内部函数声明 ===================== */
static uint16_t OneNet_NextPacketId(void);
static uint8_t OneNet_SendPacket(MQTT_PACKET_STRUCTURE *packet);
static uint8_t OneNet_SendConnect(void);
static uint8_t OneNet_SendSubscribe(void);
static uint8_t OneNet_SendPing(void);
static uint8_t OneNet_SendPublishResponse(uint8_t qos, uint16_t packet_id);
static uint8_t OneNet_SendPropertySetReply(const char *message_id, uint16_t code);
static void OneNet_RequestPropertySetReply(const char *message_id, uint16_t code);
static int OneNet_FormatPayload(char *buffer, uint16_t size, const OneNetTelemetry_t *data);
static void OneNet_HandlePropertySetPayload(const char *payload);
static void OneNet_HandleMqttFrame(uint8_t *data, uint16_t len);
static void OneNet_RxCallback(ESP_DataPacket_t *packet);

/* ===================== Packet ID 管理 ===================== */
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

/* ===================== 发送 MQTT 包 ===================== */
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

/* ===================== MQTT CONNECT ===================== */
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

/* ===================== MQTT SUBSCRIBE ===================== */
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

/* ===================== MQTT PING ===================== */
static uint8_t OneNet_SendPing(void)
{
    MQTT_PACKET_STRUCTURE packet = {0};

    if (MQTT_PacketPing(&packet) != 0U)
    {
        return 0U;
    }

    return OneNet_SendPacket(&packet);
}

/* ===================== PUBLISH ACK ===================== */
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

/* ===================== Property Set Reply ===================== */
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

/* ===================== 物模型数据上报 JSON 格式化 ===================== */
static int OneNet_FormatPayload(char *buffer, uint16_t size, const OneNetTelemetry_t *data)
{
    uint32_t message_id = onenet_message_id;
    int written = 0;

    if ((buffer == NULL) || (data == NULL) || (size == 0U))
    {
        return -1;
    }

    written = snprintf(buffer,
                       size,
                       "{\"id\":\"%lu\",\"version\":\"1.0\",\"params\":{"
                       "\"Face_count\":{\"value\":%ld},"
                       "\"Fingerprint_count\":{\"value\":%ld},"
                       "\"Password\":{\"value\":\"%s\"},"
                       "\"RFID_count\":{\"value\":%ld},"
                       "\"Servo\":{\"value\":%s}}}",
                       (unsigned long)message_id,
                       (long)data->face_count,
                       (long)data->fingerprint_count,
                       data->password,
                       (long)data->rfid_count,
                       (data->servo != 0U) ? "true" : "false");

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

/* ===================== 解析云端属性下发 ===================== */
static void OneNet_HandlePropertySetPayload(const char *payload)
{
    cJSON *root = NULL;
    cJSON *params = NULL;
    cJSON *id_item = NULL;
    cJSON *servo_item = NULL;
    cJSON *pwd_item = NULL;
    char message_id[64];
    uint8_t handled = 0U;

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

    /* 提取消息 ID */
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

    /* 提取 params */
    params = cJSON_GetObjectItem(root, "params");

    /* ---- 处理 Servo 属性下发 ---- */
    if (params != NULL)
    {
        servo_item = cJSON_GetObjectItem(params, "Servo");
    }

    if (servo_item != NULL)
    {
        uint8_t servo_on = 0U;
        cJSON *servo_val = servo_item;

        /* 如果是 {"value": ...} 形式，取出 value */
        if ((servo_val->type & 0xFF) == cJSON_Object)
        {
            servo_val = cJSON_GetObjectItem(servo_val, "value");
        }

        if (servo_val != NULL)
        {
            if ((servo_val->type & 0xFF) == cJSON_True)
            {
                servo_on = 1U;
            }
            else if ((servo_val->type & 0xFF) == cJSON_False)
            {
                servo_on = 0U;
            }
            else if ((servo_val->type & 0xFF) == cJSON_Number)
            {
                servo_on = (servo_val->valueint != 0) ? 1U : 0U;
            }
            else if (((servo_val->type & 0xFF) == cJSON_String) && (servo_val->valuestring != NULL))
            {
                if ((strcmp(servo_val->valuestring, "1") == 0) ||
                    (strcmp(servo_val->valuestring, "true") == 0))
                {
                    servo_on = 1U;
                }
            }

            if (onenet_servo_control_callback != NULL)
            {
                onenet_servo_control_callback(servo_on);
                handled = 1U;
            }
        }
    }

    /* ---- 处理 Password 属性下发 ---- */
    if (params != NULL)
    {
        pwd_item = cJSON_GetObjectItem(params, "Password");
    }

    if (pwd_item != NULL)
    {
        cJSON *pwd_val = pwd_item;

        /* 如果是 {"value": ...} 形式，取出 value */
        if ((pwd_val->type & 0xFF) == cJSON_Object)
        {
            pwd_val = cJSON_GetObjectItem(pwd_val, "value");
        }

        if ((pwd_val != NULL) &&
            ((pwd_val->type & 0xFF) == cJSON_String) &&
            (pwd_val->valuestring != NULL))
        {
            if (onenet_password_set_callback != NULL)
            {
                onenet_password_set_callback(pwd_val->valuestring);
                handled = 1U;
            }
        }
    }

    OneNet_RequestPropertySetReply(message_id, (handled != 0U) ? 200U : 400U);
    cJSON_Delete(root);
}

/* ===================== MQTT 帧解析 ===================== */
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

/* ===================== ESP 回调 ===================== */
static void OneNet_RxCallback(ESP_DataPacket_t *packet)
{
    if ((packet == NULL) || (packet->len == 0U))
    {
        return;
    }

    OneNet_HandleMqttFrame(packet->data, packet->len);
}

/* ===================== 公开 API ===================== */

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

    ESP_RegisterCallback(OneNet_RxCallback);

    if (ESP_Init() != ESP_OK)
    {
        return 0U;
    }

    return 1U;
}

void OneNet_RegisterServoControlCallback(OneNet_ServoControlCallback_t cb)
{
    onenet_servo_control_callback = cb;
}

void OneNet_RegisterPasswordSetCallback(OneNet_PasswordSetCallback_t cb)
{
    onenet_password_set_callback = cb;
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

uint8_t OneNet_IsSubscribed(void)
{
    return onenet_subscribed;
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
