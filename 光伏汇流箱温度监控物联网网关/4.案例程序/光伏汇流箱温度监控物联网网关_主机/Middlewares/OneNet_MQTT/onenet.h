#ifndef _ONENET_H_
#define _ONENET_H_

#include <stdint.h>

typedef enum
{
    ONENET_VALUE_INT = 0,
    ONENET_VALUE_FLOAT,
    ONENET_VALUE_BOOL,
    ONENET_VALUE_STRING
} OneNetValueType_t;

typedef struct
{
    const char *name;
    OneNetValueType_t type;
    union
    {
        int32_t i32;
        float f32;
        uint8_t boolean;
        const char *str;
    } value;
    uint8_t precision;
} OneNetProperty_t;

typedef uint8_t (*OneNetPropertySetCallback_t)(uint8_t has_threshold,
                                               float threshold,
                                               uint8_t has_report_period,
                                               int32_t report_period);

uint8_t OneNet_Init(void);
void OneNet_Process(void);

uint8_t OneNet_IsConnected(void);
uint8_t OneNet_IsReady(void);
uint16_t OneNet_GetLastPostReplyCode(void);
const char *OneNet_GetLastPostReplyMsg(void);

void OneNet_RegisterPropertySetCallback(OneNetPropertySetCallback_t callback);
uint8_t OneNet_PublishProperties(const OneNetProperty_t *properties, uint8_t count);

#endif /* _ONENET_H_ */
