#ifndef _ONENET_H_
#define _ONENET_H_

#include <stdint.h>

/* OneNET 遥测数据结构：对应平台属性字段 */
typedef struct
{
    /* 声光报警状态：0 关闭，非 0 开启 */
    uint8_t av_alm;
    /* 火焰检测状态：0 无火焰，非 0 检测到火焰 */
    uint8_t flame;
    /* 湿度值 */
    int32_t hum;
    /* 烟雾值 */
    int32_t smoke;
    /* 温度值 */
    int32_t temp;
} OneNetTelemetry_t;

typedef void (*OneNet_AlarmControlCallback_t)(uint8_t alarm_on);

/* 初始化 OneNET 模块与 ESP 通信链路 */
uint8_t OneNet_Init(void);

/* 注册云端声光报警器控制回调 */
void OneNet_RegisterAlarmControlCallback(OneNet_AlarmControlCallback_t cb);

/* OneNET 周期处理函数，需在主循环中持续调用 */
void OneNet_Process(void);

/* 查询是否已建立可用连接（TCP + MQTT） */
uint8_t OneNet_IsConnected(void);

/* 上报一帧遥测数据到 OneNET 平台 */
uint8_t OneNet_PublishTelemetry(const OneNetTelemetry_t *data);

#endif
