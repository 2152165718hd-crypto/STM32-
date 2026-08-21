#include ".\APPLICATION\EnvProto\EnvProto.h"
#include <string.h>

typedef enum
{
    ENV_PROTO_STATE_WAIT_HEADER_1 = 0,
    ENV_PROTO_STATE_WAIT_HEADER_2,
    ENV_PROTO_STATE_WAIT_VERSION,
    ENV_PROTO_STATE_WAIT_TYPE,
    ENV_PROTO_STATE_WAIT_LEN,
    ENV_PROTO_STATE_WAIT_PAYLOAD,
    ENV_PROTO_STATE_WAIT_CHECKSUM
} EnvProto_State_t;

static void EnvProto_RestartParser(EnvProto_Parser_t *parser, uint8_t byte)
{
    EnvProto_ResetParser(parser);
    if (byte == ENV_PROTO_HEADER_1)
    {
        parser->state = ENV_PROTO_STATE_WAIT_HEADER_2;
    }
}

static void EnvProto_WriteLe16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xFFU);
    dst[1] = (uint8_t)((value >> 8) & 0xFFU);
}

static uint16_t EnvProto_ReadLe16(const uint8_t *src)
{
    return (uint16_t)(((uint16_t)src[1] << 8) | src[0]);
}

static uint8_t EnvProto_BuildFrame(uint8_t type, const uint8_t *payload, uint8_t payload_len, uint8_t *out_buf, uint8_t *out_len)
{
    uint8_t index = 0U;
    uint8_t checksum = 0U;
    uint8_t i = 0U;

    if ((out_buf == NULL) || (out_len == NULL))
    {
        return 0U;
    }

    if (payload_len > ENV_PROTO_MAX_PAYLOAD_LEN)
    {
        return 0U;
    }

    if ((payload_len > 0U) && (payload == NULL))
    {
        return 0U;
    }

    out_buf[index++] = ENV_PROTO_HEADER_1;
    out_buf[index++] = ENV_PROTO_HEADER_2;
    out_buf[index++] = ENV_PROTO_VERSION;
    out_buf[index++] = type;
    out_buf[index++] = payload_len;

    checksum = (uint8_t)(ENV_PROTO_VERSION + type + payload_len);

    for (i = 0U; i < payload_len; i++)
    {
        out_buf[index++] = payload[i];
        checksum = (uint8_t)(checksum + payload[i]);
    }

    out_buf[index++] = checksum;
    *out_len = index;
    return 1U;
}

void EnvProto_ResetParser(EnvProto_Parser_t *parser)
{
    if (parser == NULL)
    {
        return;
    }

    memset(parser, 0, sizeof(*parser));
    parser->state = ENV_PROTO_STATE_WAIT_HEADER_1;
}

uint8_t EnvProto_ParseByte(EnvProto_Parser_t *parser, uint8_t byte, EnvProto_Frame_t *out_frame)
{
    if ((parser == NULL) || (out_frame == NULL))
    {
        return 0U;
    }

    switch ((EnvProto_State_t)parser->state)
    {
    case ENV_PROTO_STATE_WAIT_HEADER_1:
        if (byte == ENV_PROTO_HEADER_1)
        {
            parser->state = ENV_PROTO_STATE_WAIT_HEADER_2;
        }
        break;

    case ENV_PROTO_STATE_WAIT_HEADER_2:
        if (byte == ENV_PROTO_HEADER_2)
        {
            parser->state = ENV_PROTO_STATE_WAIT_VERSION;
            parser->checksum = 0U;
        }
        else if (byte != ENV_PROTO_HEADER_1)
        {
            parser->state = ENV_PROTO_STATE_WAIT_HEADER_1;
        }
        break;

    case ENV_PROTO_STATE_WAIT_VERSION:
        if (byte != ENV_PROTO_VERSION)
        {
            EnvProto_RestartParser(parser, byte);
            break;
        }
        parser->version = byte;
        parser->checksum = byte;
        parser->state = ENV_PROTO_STATE_WAIT_TYPE;
        break;

    case ENV_PROTO_STATE_WAIT_TYPE:
        parser->type = byte;
        parser->checksum = (uint8_t)(parser->checksum + byte);
        parser->state = ENV_PROTO_STATE_WAIT_LEN;
        break;

    case ENV_PROTO_STATE_WAIT_LEN:
        if (byte > ENV_PROTO_MAX_PAYLOAD_LEN)
        {
            EnvProto_RestartParser(parser, byte);
            break;
        }
        parser->len = byte;
        parser->index = 0U;
        parser->checksum = (uint8_t)(parser->checksum + byte);
        parser->state = (byte == 0U) ? ENV_PROTO_STATE_WAIT_CHECKSUM : ENV_PROTO_STATE_WAIT_PAYLOAD;
        break;

    case ENV_PROTO_STATE_WAIT_PAYLOAD:
        parser->payload[parser->index++] = byte;
        parser->checksum = (uint8_t)(parser->checksum + byte);
        if (parser->index >= parser->len)
        {
            parser->state = ENV_PROTO_STATE_WAIT_CHECKSUM;
        }
        break;

    case ENV_PROTO_STATE_WAIT_CHECKSUM:
        if (parser->checksum == byte)
        {
            out_frame->type = parser->type;
            out_frame->len = parser->len;
            if (parser->len > 0U)
            {
                memcpy(out_frame->payload, parser->payload, parser->len);
            }
            EnvProto_ResetParser(parser);
            return 1U;
        }
        EnvProto_RestartParser(parser, byte);
        break;

    default:
        EnvProto_ResetParser(parser);
        break;
    }

    return 0U;
}

uint8_t EnvProto_BuildSensorReport(uint8_t valid_bits,
                                   uint8_t temp_c,
                                   uint8_t hum_pct,
                                   uint16_t pm25_ugm3,
                                   uint16_t mq135_mv,
                                   uint16_t light_lux,
                                   uint8_t *out_buf,
                                   uint8_t *out_len)
{
    uint8_t payload[ENV_PROTO_SENSOR_PAYLOAD_LEN];

    payload[0] = valid_bits;
    payload[1] = temp_c;
    payload[2] = hum_pct;
    EnvProto_WriteLe16(&payload[3], pm25_ugm3);
    EnvProto_WriteLe16(&payload[5], mq135_mv);
    EnvProto_WriteLe16(&payload[7], light_lux);

    return EnvProto_BuildFrame(ENV_PROTO_TYPE_SENSOR_REPORT, payload, ENV_PROTO_SENSOR_PAYLOAD_LEN, out_buf, out_len);
}

uint8_t EnvProto_BuildQueryNow(uint8_t *out_buf, uint8_t *out_len)
{
    return EnvProto_BuildFrame(ENV_PROTO_TYPE_QUERY_NOW, NULL, 0U, out_buf, out_len);
}

uint8_t EnvProto_DecodeSensorReport(const EnvProto_Frame_t *frame, EnvProto_SensorPayload_t *out_payload)
{
    if ((frame == NULL) || (out_payload == NULL))
    {
        return 0U;
    }

    if ((frame->type != ENV_PROTO_TYPE_SENSOR_REPORT) || (frame->len != ENV_PROTO_SENSOR_PAYLOAD_LEN))
    {
        return 0U;
    }

    out_payload->valid_bits = frame->payload[0];
    out_payload->temp_c = frame->payload[1];
    out_payload->hum_pct = frame->payload[2];
    out_payload->pm25_ugm3 = EnvProto_ReadLe16(&frame->payload[3]);
    out_payload->mq135_mv = EnvProto_ReadLe16(&frame->payload[5]);
    out_payload->light_lux = EnvProto_ReadLe16(&frame->payload[7]);
    return 1U;
}
