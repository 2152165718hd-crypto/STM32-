#include ".\APPLICATION\EnvProto\EnvProto.h"
#include <string.h>

typedef enum
{
    ENVPROTO_WAIT_HEAD1 = 0,
    ENVPROTO_WAIT_HEAD2,
    ENVPROTO_WAIT_TYPE,
    ENVPROTO_WAIT_SEQ,
    ENVPROTO_WAIT_LEN,
    ENVPROTO_WAIT_PAYLOAD,
    ENVPROTO_WAIT_CRC_LOW,
    ENVPROTO_WAIT_CRC_HIGH
} EnvProto_ParseState_t;

void EnvProto_ResetParser(EnvProto_Parser_t *parser)
{
    if (parser == NULL)
    {
        return;
    }

    memset(parser, 0, sizeof(EnvProto_Parser_t));
    parser->state = ENVPROTO_WAIT_HEAD1;
}

uint16_t EnvProto_Crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFFU;
    uint8_t bit;

    if (data == NULL)
    {
        return 0U;
    }

    while (len-- != 0U)
    {
        crc ^= *data++;
        for (bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 0x0001U) != 0U)
            {
                crc >>= 1U;
                crc ^= 0xA001U;
            }
            else
            {
                crc >>= 1U;
            }
        }
    }

    return crc;
}

uint16_t EnvProto_BuildFrame(uint8_t type, uint8_t seq, const uint8_t *payload,
                             uint8_t payload_len, uint8_t *out, uint16_t out_size)
{
    uint16_t crc;
    uint16_t body_len = (uint16_t)(3U + payload_len);

    if ((out == NULL) || (payload_len > ENVPROTO_MAX_PAYLOAD) ||
        (out_size < (uint16_t)(2U + body_len + 2U)))
    {
        return 0U;
    }
    if ((payload_len > 0U) && (payload == NULL))
    {
        return 0U;
    }

    out[0] = ENVPROTO_HEAD1;
    out[1] = ENVPROTO_HEAD2;
    out[2] = type;
    out[3] = seq;
    out[4] = payload_len;
    if (payload_len > 0U)
    {
        memcpy(&out[5], payload, payload_len);
    }

    crc = EnvProto_Crc16(&out[2], body_len);
    out[5U + payload_len] = (uint8_t)(crc & 0x00FFU);
    out[6U + payload_len] = (uint8_t)((crc >> 8U) & 0x00FFU);

    return (uint16_t)(7U + payload_len);
}

uint16_t EnvProto_BuildTempReport(uint8_t seq, uint8_t node_id, int16_t temperature_x10,
                                  uint8_t status, uint8_t *out, uint16_t out_size)
{
    uint8_t payload[4];

    payload[0] = node_id;
    payload[1] = (uint8_t)(((uint16_t)temperature_x10 >> 8U) & 0x00FFU);
    payload[2] = (uint8_t)((uint16_t)temperature_x10 & 0x00FFU);
    payload[3] = status;

    return EnvProto_BuildFrame(ENVPROTO_TYPE_TEMP_REPORT, seq, payload,
                               (uint8_t)sizeof(payload), out, out_size);
}

uint8_t EnvProto_ParseTempReport(const EnvProto_Frame_t *frame, EnvProto_TempReport_t *report)
{
    uint16_t raw;

    if ((frame == NULL) || (report == NULL) ||
        (frame->type != ENVPROTO_TYPE_TEMP_REPORT) || (frame->len < 4U))
    {
        return 0U;
    }

    raw = (uint16_t)(((uint16_t)frame->payload[1] << 8U) | frame->payload[2]);
    report->node_id = frame->payload[0];
    report->temperature_x10 = (int16_t)raw;
    report->status = frame->payload[3];

    return 1U;
}

uint8_t EnvProto_ParseByte(EnvProto_Parser_t *parser, uint8_t byte, EnvProto_Frame_t *frame)
{
    uint16_t crc_calc;
    uint16_t crc_recv;

    if ((parser == NULL) || (frame == NULL))
    {
        return 0U;
    }

    switch ((EnvProto_ParseState_t)parser->state)
    {
    case ENVPROTO_WAIT_HEAD1:
        if (byte == ENVPROTO_HEAD1)
        {
            parser->state = ENVPROTO_WAIT_HEAD2;
        }
        break;

    case ENVPROTO_WAIT_HEAD2:
        if (byte == ENVPROTO_HEAD2)
        {
            parser->index = 0U;
            parser->state = ENVPROTO_WAIT_TYPE;
        }
        else
        {
            parser->state = (byte == ENVPROTO_HEAD1) ? ENVPROTO_WAIT_HEAD2 : ENVPROTO_WAIT_HEAD1;
        }
        break;

    case ENVPROTO_WAIT_TYPE:
        parser->buf[parser->index++] = byte;
        parser->frame.type = byte;
        parser->state = ENVPROTO_WAIT_SEQ;
        break;

    case ENVPROTO_WAIT_SEQ:
        parser->buf[parser->index++] = byte;
        parser->frame.seq = byte;
        parser->state = ENVPROTO_WAIT_LEN;
        break;

    case ENVPROTO_WAIT_LEN:
        if (byte > ENVPROTO_MAX_PAYLOAD)
        {
            EnvProto_ResetParser(parser);
            break;
        }
        parser->buf[parser->index++] = byte;
        parser->frame.len = byte;
        if (byte == 0U)
        {
            parser->state = ENVPROTO_WAIT_CRC_LOW;
        }
        else
        {
            parser->state = ENVPROTO_WAIT_PAYLOAD;
        }
        break;

    case ENVPROTO_WAIT_PAYLOAD:
        parser->frame.payload[parser->index - 3U] = byte;
        parser->buf[parser->index++] = byte;
        if ((parser->index - 3U) >= parser->frame.len)
        {
            parser->state = ENVPROTO_WAIT_CRC_LOW;
        }
        break;

    case ENVPROTO_WAIT_CRC_LOW:
        parser->crc_low = byte;
        parser->state = ENVPROTO_WAIT_CRC_HIGH;
        break;

    case ENVPROTO_WAIT_CRC_HIGH:
        crc_recv = (uint16_t)(((uint16_t)byte << 8U) | parser->crc_low);
        crc_calc = EnvProto_Crc16(parser->buf, parser->index);
        if (crc_recv == crc_calc)
        {
            *frame = parser->frame;
            EnvProto_ResetParser(parser);
            return 1U;
        }
        EnvProto_ResetParser(parser);
        break;

    default:
        EnvProto_ResetParser(parser);
        break;
    }

    return 0U;
}
