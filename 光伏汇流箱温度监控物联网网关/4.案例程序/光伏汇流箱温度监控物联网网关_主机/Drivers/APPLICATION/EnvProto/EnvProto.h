#ifndef __ENV_PROTO_H__
#define __ENV_PROTO_H__

#include <stdint.h>

#define ENVPROTO_HEAD1              0xA5U
#define ENVPROTO_HEAD2              0x5AU
#define ENVPROTO_MAX_PAYLOAD        32U

#define ENVPROTO_TYPE_TEMP_REPORT   0x01U

#define ENVPROTO_TEMP_STATUS_OK     0x00U
#define ENVPROTO_TEMP_STATUS_ERROR  0x01U

typedef struct
{
    uint8_t type;
    uint8_t seq;
    uint8_t len;
    uint8_t payload[ENVPROTO_MAX_PAYLOAD];
} EnvProto_Frame_t;

typedef struct
{
    uint8_t state;
    uint8_t index;
    uint8_t crc_low;
    uint8_t buf[3U + ENVPROTO_MAX_PAYLOAD];
    EnvProto_Frame_t frame;
} EnvProto_Parser_t;

typedef struct
{
    uint8_t node_id;
    int16_t temperature_x10;
    uint8_t status;
} EnvProto_TempReport_t;

void EnvProto_ResetParser(EnvProto_Parser_t *parser);
uint8_t EnvProto_ParseByte(EnvProto_Parser_t *parser, uint8_t byte, EnvProto_Frame_t *frame);
uint16_t EnvProto_Crc16(const uint8_t *data, uint16_t len);
uint16_t EnvProto_BuildFrame(uint8_t type, uint8_t seq, const uint8_t *payload,
                             uint8_t payload_len, uint8_t *out, uint16_t out_size);
uint16_t EnvProto_BuildTempReport(uint8_t seq, uint8_t node_id, int16_t temperature_x10,
                                  uint8_t status, uint8_t *out, uint16_t out_size);
uint8_t EnvProto_ParseTempReport(const EnvProto_Frame_t *frame, EnvProto_TempReport_t *report);

#endif /* __ENV_PROTO_H__ */
