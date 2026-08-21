#ifndef __TCP_PROTOCOL_H
#define __TCP_PROTOCOL_H

#include <stdint.h>

#define TCP_PROTOCOL_VERSION 1u
#define TCP_PROTOCOL_HEADER_SIZE 16u
#define TCP_PROTOCOL_MAX_LINKS 5u
#define TCP_PROTOCOL_RX_BUFFER_SIZE 288u
#define TCP_PROTOCOL_MAX_RX_BODY_SIZE 240u

typedef enum
{
    TCP_MSG_LOGIN_REQ = 0x0001u,
    TCP_MSG_LOGIN_RSP = 0x0002u,
    TCP_MSG_PING = 0x0003u,
    TCP_MSG_PONG = 0x0004u,
    TCP_MSG_STATUS_QUERY = 0x0010u,
    TCP_MSG_STATUS_RSP = 0x0011u,
    TCP_MSG_STATUS_PUSH = 0x0012u,
    TCP_MSG_AUDIO_REPORT = 0x0020u,
    TCP_MSG_VIBRATION_REPORT = 0x0021u,
    TCP_MSG_ALARM_REPORT = 0x0030u,
    TCP_MSG_CONFIG_SET = 0x0040u,
    TCP_MSG_CONFIG_RSP = 0x0041u,
    TCP_MSG_CONTROL_CMD = 0x0050u,
    TCP_MSG_CONTROL_RSP = 0x0051u,
    TCP_MSG_HISTORY_QUERY = 0x0060u,
    TCP_MSG_HISTORY_RSP = 0x0061u,
    TCP_MSG_ERROR_RSP = 0x00FFu
} TcpProtocol_MessageType_t;

typedef enum
{
    TCP_ERR_OK = 0,
    TCP_ERR_BAD_MAGIC,
    TCP_ERR_UNSUPPORTED_VERSION,
    TCP_ERR_FRAME_TOO_LARGE,
    TCP_ERR_BAD_JSON,
    TCP_ERR_UNKNOWN_TYPE,
    TCP_ERR_INVALID_FIELD,
    TCP_ERR_INVALID_VALUE,
    TCP_ERR_NOT_LOGIN,
    TCP_ERR_TIMEOUT
} TcpProtocol_ErrorCode_t;

typedef struct
{
    uint8_t link_id;
    TcpProtocol_MessageType_t type;
    uint32_t seq;
    const char *json;
    uint16_t json_len;
} TcpProtocol_Message_t;

typedef void (*TcpProtocol_MessageCallback_t)(const TcpProtocol_Message_t *message);
typedef void (*TcpProtocol_ErrorCallback_t)(uint8_t link_id, uint32_t seq, TcpProtocol_ErrorCode_t error);

void TcpProtocol_Init(TcpProtocol_MessageCallback_t message_callback, TcpProtocol_ErrorCallback_t error_callback);
void TcpProtocol_ResetLink(uint8_t link_id);
void TcpProtocol_ProcessBytes(uint8_t link_id, const uint8_t *data, uint16_t len);
uint8_t TcpProtocol_BuildHeader(TcpProtocol_MessageType_t type, uint32_t seq, uint32_t body_len, uint8_t *out_header, uint16_t out_size);
const char *TcpProtocol_ErrorText(TcpProtocol_ErrorCode_t error);
const char *TcpProtocol_MessageTypeText(TcpProtocol_MessageType_t type);

#endif
