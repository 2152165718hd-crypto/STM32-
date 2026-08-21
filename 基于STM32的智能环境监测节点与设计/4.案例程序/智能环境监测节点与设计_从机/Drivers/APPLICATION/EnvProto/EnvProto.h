#ifndef __ENV_PROTO_H__
#define __ENV_PROTO_H__

#include "stm32f1xx_hal.h"

#define ENV_PROTO_HEADER_1            0xAAU
#define ENV_PROTO_HEADER_2            0x55U
#define ENV_PROTO_VERSION             0x01U

#define ENV_PROTO_TYPE_SENSOR_REPORT  0x01U
#define ENV_PROTO_TYPE_QUERY_NOW      0x02U

#define ENV_PROTO_SENSOR_PAYLOAD_LEN  9U
#define ENV_PROTO_MAX_PAYLOAD_LEN     16U

#define ENV_PROTO_VALID_DHT11         0x01U
#define ENV_PROTO_VALID_PM25          0x02U
#define ENV_PROTO_VALID_MQ135         0x04U
#define ENV_PROTO_VALID_BH1750        0x08U

typedef struct
{
    uint8_t type;
    uint8_t len;
    uint8_t payload[ENV_PROTO_MAX_PAYLOAD_LEN];
} EnvProto_Frame_t;

typedef struct
{
    uint8_t valid_bits;
    uint8_t temp_c;
    uint8_t hum_pct;
    uint16_t pm25_ugm3;
    uint16_t mq135_mv;
    uint16_t light_lux;
} EnvProto_SensorPayload_t;

typedef struct
{
    uint8_t state;
    uint8_t version;
    uint8_t type;
    uint8_t len;
    uint8_t index;
    uint8_t checksum;
    uint8_t payload[ENV_PROTO_MAX_PAYLOAD_LEN];
} EnvProto_Parser_t;

void EnvProto_ResetParser(EnvProto_Parser_t *parser);
uint8_t EnvProto_ParseByte(EnvProto_Parser_t *parser, uint8_t byte, EnvProto_Frame_t *out_frame);

uint8_t EnvProto_BuildSensorReport(uint8_t valid_bits,
                                   uint8_t temp_c,
                                   uint8_t hum_pct,
                                   uint16_t pm25_ugm3,
                                   uint16_t mq135_mv,
                                   uint16_t light_lux,
                                   uint8_t *out_buf,
                                   uint8_t *out_len);

uint8_t EnvProto_BuildQueryNow(uint8_t *out_buf, uint8_t *out_len);
uint8_t EnvProto_DecodeSensorReport(const EnvProto_Frame_t *frame, EnvProto_SensorPayload_t *out_payload);

#endif /* __ENV_PROTO_H__ */
