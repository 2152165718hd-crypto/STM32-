#include ".\APPLICATION\TcpProtocol\TcpProtocol.h"

#include <string.h>

#define TCP_MAGIC_0 ((uint8_t)'H')
#define TCP_MAGIC_1 ((uint8_t)'S')

typedef struct
{
    uint8_t buffer[TCP_PROTOCOL_RX_BUFFER_SIZE + 1u];
    uint16_t length;
} TcpProtocol_LinkParser_t;

static TcpProtocol_LinkParser_t s_links[TCP_PROTOCOL_MAX_LINKS];
static TcpProtocol_MessageCallback_t s_message_callback = 0;
static TcpProtocol_ErrorCallback_t s_error_callback = 0;

static uint16_t TcpProtocol_ReadU16Be(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]);
}

static uint32_t TcpProtocol_ReadU32Be(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) |
           ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) |
           ((uint32_t)data[3]);
}

static void TcpProtocol_WriteU16Be(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)((value >> 8) & 0xFFu);
    data[1] = (uint8_t)(value & 0xFFu);
}

static void TcpProtocol_WriteU32Be(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)((value >> 24) & 0xFFu);
    data[1] = (uint8_t)((value >> 16) & 0xFFu);
    data[2] = (uint8_t)((value >> 8) & 0xFFu);
    data[3] = (uint8_t)(value & 0xFFu);
}

static void TcpProtocol_ReportError(uint8_t link_id, uint32_t seq, TcpProtocol_ErrorCode_t error)
{
    if (s_error_callback != 0)
    {
        s_error_callback(link_id, seq, error);
    }
}

static void TcpProtocol_DropBytes(TcpProtocol_LinkParser_t *parser, uint16_t count)
{
    if ((parser == 0) || (count == 0u))
    {
        return;
    }

    if (count >= parser->length)
    {
        parser->length = 0u;
        return;
    }

    memmove(parser->buffer, &parser->buffer[count], (uint16_t)(parser->length - count));
    parser->length = (uint16_t)(parser->length - count);
}

static uint16_t TcpProtocol_FindMagic(const TcpProtocol_LinkParser_t *parser)
{
    uint16_t i;

    if (parser == 0)
    {
        return 0xFFFFu;
    }

    for (i = 0u; (uint16_t)(i + 1u) < parser->length; i++)
    {
        if ((parser->buffer[i] == TCP_MAGIC_0) && (parser->buffer[i + 1u] == TCP_MAGIC_1))
        {
            return i;
        }
    }

    return 0xFFFFu;
}

static void TcpProtocol_ProcessLink(uint8_t link_id)
{
    TcpProtocol_LinkParser_t *parser;

    if (link_id >= TCP_PROTOCOL_MAX_LINKS)
    {
        return;
    }

    parser = &s_links[link_id];
    while (parser->length >= TCP_PROTOCOL_HEADER_SIZE)
    {
        uint16_t magic_offset;
        uint16_t msg_type_value;
        uint32_t seq;
        uint32_t body_len;
        uint32_t frame_len;
        TcpProtocol_Message_t message;

        if ((parser->buffer[0] != TCP_MAGIC_0) || (parser->buffer[1] != TCP_MAGIC_1))
        {
            magic_offset = TcpProtocol_FindMagic(parser);
            TcpProtocol_ReportError(link_id, 0u, TCP_ERR_BAD_MAGIC);
            if (magic_offset == 0xFFFFu)
            {
                if (parser->buffer[parser->length - 1u] == TCP_MAGIC_0)
                {
                    parser->buffer[0] = TCP_MAGIC_0;
                    parser->length = 1u;
                }
                else
                {
                    parser->length = 0u;
                }
            }
            else
            {
                TcpProtocol_DropBytes(parser, magic_offset);
            }
            continue;
        }

        if ((parser->buffer[2] != TCP_PROTOCOL_VERSION) ||
            (parser->buffer[3] != TCP_PROTOCOL_HEADER_SIZE))
        {
            TcpProtocol_ReportError(link_id, 0u, TCP_ERR_UNSUPPORTED_VERSION);
            TcpProtocol_DropBytes(parser, 1u);
            continue;
        }

        msg_type_value = TcpProtocol_ReadU16Be(&parser->buffer[4]);
        seq = TcpProtocol_ReadU32Be(&parser->buffer[8]);
        body_len = TcpProtocol_ReadU32Be(&parser->buffer[12]);

        if (body_len > TCP_PROTOCOL_MAX_RX_BODY_SIZE)
        {
            TcpProtocol_ReportError(link_id, seq, TCP_ERR_FRAME_TOO_LARGE);
            parser->length = 0u;
            continue;
        }

        frame_len = (uint32_t)TCP_PROTOCOL_HEADER_SIZE + body_len;
        if (frame_len > parser->length)
        {
            break;
        }

        if ((body_len != 0u) &&
            ((parser->buffer[TCP_PROTOCOL_HEADER_SIZE] != '{') || (parser->buffer[frame_len - 1u] != '}')))
        {
            TcpProtocol_ReportError(link_id, seq, TCP_ERR_BAD_JSON);
            TcpProtocol_DropBytes(parser, (uint16_t)frame_len);
            continue;
        }

        parser->buffer[frame_len] = '\0';
        message.link_id = link_id;
        message.type = (TcpProtocol_MessageType_t)msg_type_value;
        message.seq = seq;
        message.json = (const char *)&parser->buffer[TCP_PROTOCOL_HEADER_SIZE];
        message.json_len = (uint16_t)body_len;

        if (s_message_callback != 0)
        {
            s_message_callback(&message);
        }

        TcpProtocol_DropBytes(parser, (uint16_t)frame_len);
    }
}

void TcpProtocol_Init(TcpProtocol_MessageCallback_t message_callback, TcpProtocol_ErrorCallback_t error_callback)
{
    memset(s_links, 0, sizeof(s_links));
    s_message_callback = message_callback;
    s_error_callback = error_callback;
}

void TcpProtocol_ResetLink(uint8_t link_id)
{
    if (link_id < TCP_PROTOCOL_MAX_LINKS)
    {
        s_links[link_id].length = 0u;
    }
}

void TcpProtocol_ProcessBytes(uint8_t link_id, const uint8_t *data, uint16_t len)
{
    TcpProtocol_LinkParser_t *parser;
    uint16_t offset = 0u;

    if ((link_id >= TCP_PROTOCOL_MAX_LINKS) || (data == 0) || (len == 0u))
    {
        return;
    }

    parser = &s_links[link_id];
    while (offset < len)
    {
        uint16_t remain = (uint16_t)(TCP_PROTOCOL_RX_BUFFER_SIZE - parser->length);
        uint16_t copy_len = (uint16_t)(len - offset);

        if (remain == 0u)
        {
            parser->length = 0u;
            TcpProtocol_ReportError(link_id, 0u, TCP_ERR_FRAME_TOO_LARGE);
            return;
        }

        if (copy_len > remain)
        {
            copy_len = remain;
        }

        memcpy(&parser->buffer[parser->length], &data[offset], copy_len);
        parser->length = (uint16_t)(parser->length + copy_len);
        offset = (uint16_t)(offset + copy_len);
        TcpProtocol_ProcessLink(link_id);
    }
}

uint8_t TcpProtocol_BuildHeader(TcpProtocol_MessageType_t type, uint32_t seq, uint32_t body_len, uint8_t *out_header, uint16_t out_size)
{
    if ((out_header == 0) || (out_size < TCP_PROTOCOL_HEADER_SIZE))
    {
        return 0u;
    }

    out_header[0] = TCP_MAGIC_0;
    out_header[1] = TCP_MAGIC_1;
    out_header[2] = TCP_PROTOCOL_VERSION;
    out_header[3] = TCP_PROTOCOL_HEADER_SIZE;
    TcpProtocol_WriteU16Be(&out_header[4], (uint16_t)type);
    out_header[6] = 0u;
    out_header[7] = 0u;
    TcpProtocol_WriteU32Be(&out_header[8], seq);
    TcpProtocol_WriteU32Be(&out_header[12], body_len);
    return 1u;
}

const char *TcpProtocol_ErrorText(TcpProtocol_ErrorCode_t error)
{
    switch (error)
    {
    case TCP_ERR_OK:
        return "OK";
    case TCP_ERR_BAD_MAGIC:
        return "BAD_MAGIC";
    case TCP_ERR_UNSUPPORTED_VERSION:
        return "UNSUPPORTED_VERSION";
    case TCP_ERR_FRAME_TOO_LARGE:
        return "FRAME_TOO_LARGE";
    case TCP_ERR_BAD_JSON:
        return "BAD_JSON";
    case TCP_ERR_UNKNOWN_TYPE:
        return "UNKNOWN_TYPE";
    case TCP_ERR_INVALID_FIELD:
        return "INVALID_FIELD";
    case TCP_ERR_INVALID_VALUE:
        return "INVALID_VALUE";
    case TCP_ERR_NOT_LOGIN:
        return "NOT_LOGIN";
    case TCP_ERR_TIMEOUT:
        return "TIMEOUT";
    default:
        return "UNKNOWN";
    }
}

const char *TcpProtocol_MessageTypeText(TcpProtocol_MessageType_t type)
{
    switch (type)
    {
    case TCP_MSG_LOGIN_REQ:
        return "LOGIN_REQ";
    case TCP_MSG_LOGIN_RSP:
        return "LOGIN_RSP";
    case TCP_MSG_PING:
        return "PING";
    case TCP_MSG_PONG:
        return "PONG";
    case TCP_MSG_STATUS_QUERY:
        return "STATUS_QUERY";
    case TCP_MSG_STATUS_RSP:
        return "STATUS_RSP";
    case TCP_MSG_STATUS_PUSH:
        return "STATUS_PUSH";
    case TCP_MSG_AUDIO_REPORT:
        return "AUDIO_REPORT";
    case TCP_MSG_VIBRATION_REPORT:
        return "VIBRATION_REPORT";
    case TCP_MSG_ALARM_REPORT:
        return "ALARM_REPORT";
    case TCP_MSG_CONFIG_SET:
        return "CONFIG_SET";
    case TCP_MSG_CONFIG_RSP:
        return "CONFIG_RSP";
    case TCP_MSG_CONTROL_CMD:
        return "CONTROL_CMD";
    case TCP_MSG_CONTROL_RSP:
        return "CONTROL_RSP";
    case TCP_MSG_HISTORY_QUERY:
        return "HISTORY_QUERY";
    case TCP_MSG_HISTORY_RSP:
        return "HISTORY_RSP";
    case TCP_MSG_ERROR_RSP:
        return "ERROR_RSP";
    default:
        return "UNKNOWN";
    }
}
