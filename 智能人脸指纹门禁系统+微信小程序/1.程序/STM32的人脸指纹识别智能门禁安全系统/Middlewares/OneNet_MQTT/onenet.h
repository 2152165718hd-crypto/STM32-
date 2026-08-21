#ifndef _ONENET_H_
#define _ONENET_H_

#include <stdint.h>

/* OneNET 遥测数据结构：对应平台物模型属性 */
typedef struct
{
    /* 人脸个数 */
    int32_t face_count;
    /* 指纹数量 */
    int32_t fingerprint_count;
    /* 密码（最大20字符） */
    char password[21];
    /* 卡片数量 */
    int32_t rfid_count;
    /* 舵机状态：0 关门，1 开门 */
    uint8_t servo;
} OneNetTelemetry_t;

/* 云端舵机控制回调：servo_on = 1 开门，0 关门 */
typedef void (*OneNet_ServoControlCallback_t)(uint8_t servo_on);

/* 云端密码修改回调：password 为新密码字符串 */
typedef void (*OneNet_PasswordSetCallback_t)(const char *password);

/* 初始化 OneNET 模块与 ESP 通信链路 */
uint8_t OneNet_Init(void);

/* 注册云端舵机控制回调 */
void OneNet_RegisterServoControlCallback(OneNet_ServoControlCallback_t cb);

/* 注册云端密码修改回调 */
void OneNet_RegisterPasswordSetCallback(OneNet_PasswordSetCallback_t cb);

/* OneNET 周期处理函数，需在主循环中持续调用 */
void OneNet_Process(void);

/* 查询是否已建立可用连接（TCP + MQTT） */
uint8_t OneNet_IsConnected(void);

/* 查询是否已成功订阅属性下发 Topic */
uint8_t OneNet_IsSubscribed(void);

/* 上报一帧遥测数据到 OneNET 平台 */
uint8_t OneNet_PublishTelemetry(const OneNetTelemetry_t *data);

#endif
