#ifndef __FM225_H
#define __FM225_H

#include "stm32f1xx_hal.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ======================== 硬件配置 ======================== */
#define FM225_TX_PIN GPIO_PIN_2
#define FM225_RX_PIN GPIO_PIN_3
#define FM225_GPIO_PORT GPIOA
#define FM225_UART USART2
#define FM225_UART_BAUD 115200
#define FM225_UART_IRQn USART2_IRQn

/* ======================== 协议常量 ======================== */
#define FM225_START_CODE 0xEFAA
#define FM225_NAME_SIZE 32
#define FM225_MAX_RESPONSE_SIZE 256    /* 最大响应帧数据区长度（需容纳 GET_ALL_IDS 返回的 ID 列表） */
#define FM225_RX_BUF_SIZE 512          /* 环形接收缓冲区大小（需大于最大帧长度） */
#define FM225_CMD_TIMEOUT_MS 15000U    /* 普通指令超时(ms) */
#define FM225_RESET_TIMEOUT_MS 5000U   /* RESET 指令超时(ms) */
#define FM225_RX_STALE_TIMEOUT_MS 200U /* 帧接收停滞超时(ms) */

/* ======================== 命令枚举 ======================== */
typedef enum
{
    FM225_CMD_NONE = 0x00,
    FM225_CMD_RESET = 0x01,
    FM225_CMD_GET_STATUS = 0x02,
    FM225_CMD_VERIFY = 0x12,
    FM225_CMD_ENROLL = 0x13,
    FM225_CMD_ENROLL_SINGLE = 0x1D,
    FM225_CMD_DELETE_FACE = 0x20,
    FM225_CMD_DELETE_ALL = 0x21,
    FM225_CMD_FACE_RESET = 0x23,
    FM225_CMD_GET_ALL_IDS = 0x24,
    FM225_CMD_GET_VERSION = 0x30,
    FM225_CMD_GET_SERIAL = 0x93,
} FM225_Command_t;

/* ======================== 响应类型 ======================== */
typedef enum
{
    FM225_RESP_REPLY = 0x00,
    FM225_RESP_NOTE = 0x01,
    FM225_RESP_IMAGE = 0x02,
} FM225_ResponseType_t;

/* ======================== 通知类型 ======================== */
typedef enum
{
    FM225_NOTE_READY = 0x00,
    FM225_NOTE_FACE_STATE = 0x01,
} FM225_NoteType_t;

/* ======================== 结果码 ======================== */
typedef enum
{
    FM225_OK = 0x00,
    FM225_REJECTED = 0x01,
    FM225_ABORTED = 0x02,
    FM225_ERR_CAMERA = 0x04,
    FM225_ERR_UNKNOWN = 0x05,
    FM225_ERR_INVALID_PARAM = 0x06,
    FM225_ERR_NO_MEMORY = 0x07,
    FM225_ERR_UNKNOWN_USER = 0x08,
    FM225_ERR_MAX_USER = 0x09,
    FM225_ERR_FACE_ENROLLED = 0x0A,
    FM225_ERR_LIVENESS = 0x0C,
    FM225_ERR_TIMEOUT = 0x0D,
    FM225_ERR_AUTH = 0x0E,
    FM225_ERR_READ_FILE = 0x13,
    FM225_ERR_WRITE_FILE = 0x14,
    FM225_ERR_NO_ENCRYPT = 0x15,
    FM225_ERR_NO_RGBIMAGE = 0x17,
    FM225_ERR_JPG_LARGE = 0x18,
    FM225_ERR_JPG_SMALL = 0x19,
} FM225_Result_t;

/* ======================== 人脸方向 ======================== */
typedef enum
{
    FM225_DIR_UNDEFINED = 0x00,
    FM225_DIR_MIDDLE = 0x01,
    FM225_DIR_RIGHT = 0x02,
    FM225_DIR_LEFT = 0x04,
    FM225_DIR_DOWN = 0x08,
    FM225_DIR_UP = 0x10,
} FM225_FaceDirection_t;

/* ======================== 回调函数类型 ======================== */

/** 人脸匹配成功回调: face_id, name */
typedef void (*FM225_MatchedCb_t)(int16_t face_id, const char *name);

/** 人脸未匹配回调 */
typedef void (*FM225_UnmatchedCb_t)(void);

/** 人脸扫描无效/错误回调: error_code */
typedef void (*FM225_InvalidCb_t)(uint8_t error);

/** 注册完成回调: face_id, direction */
typedef void (*FM225_EnrollDoneCb_t)(int16_t face_id, uint8_t direction);

/** 注册失败回调: error_code */
typedef void (*FM225_EnrollFailCb_t)(uint8_t error);

/** 人脸状态信息回调: status, left, top, right, bottom, yaw, pitch, roll */
typedef void (*FM225_FaceInfoCb_t)(int16_t status, int16_t left, int16_t top,
                                   int16_t right, int16_t bottom,
                                   int16_t yaw, int16_t pitch, int16_t roll);

/** 人脸数量查询回调: count */
typedef void (*FM225_FaceCountCb_t)(uint8_t count);

/* ======================== API ======================== */

/**
 * @brief  初始化 FM225 模块（含 USART2 硬件初始化）
 */
void FM225_Init(void);

/**
 * @brief  主循环中周期性调用，处理接收数据帧
 *         建议每 10~50ms 调用一次
 */
void FM225_Process(void);

/**
 * @brief  UART 接收完成回调，需在 HAL_UART_RxCpltCallback 中调用
 */
void FM225_RxCallback(void);

/**
 * @brief  UART 错误回调，需在 HAL_UART_ErrorCallback 中调用
 * @param  huart UART 句柄
 */
void FM225_ErrorCallback(UART_HandleTypeDef *huart);

/**
 * @brief  注册（录入）人脸
 * @param  name       用户名，最长 31 字节（含\\0）
 * @param  direction  人脸方向
 */
void FM225_EnrollFace(const char *name, FM225_FaceDirection_t direction);

/**
 * @brief  验证（识别）人脸
 */
void FM225_VerifyFace(void);

/**
 * @brief  删除指定 ID 的人脸
 * @param  face_id  人脸 ID
 */
void FM225_DeleteFace(int16_t face_id);

/**
 * @brief  删除所有已注册人脸
 */
void FM225_DeleteAllFaces(void);

/**
 * @brief  获取已注册人脸数量（结果通过 FaceCount 回调返回）
 */
void FM225_GetFaceCount(void);

/**
 * @brief  复位模块
 */
void FM225_Reset(void);

/**
 * @brief  中止当前操作并复位模块
 *         清除所有活动命令状态、延迟命令、录入标志，
 *         并发送 RESET 命令到模块以中止当前操作。
 *         用于用户手动退出人脸识别/录入页面时调用。
 */
void FM225_Abort(void);

/* ---- 回调注册 ---- */

void FM225_SetMatchedCallback(FM225_MatchedCb_t cb);
void FM225_SetUnmatchedCallback(FM225_UnmatchedCb_t cb);
void FM225_SetInvalidCallback(FM225_InvalidCb_t cb);
void FM225_SetEnrollDoneCallback(FM225_EnrollDoneCb_t cb);
void FM225_SetEnrollFailCallback(FM225_EnrollFailCb_t cb);
void FM225_SetFaceInfoCallback(FM225_FaceInfoCb_t cb);
void FM225_SetFaceCountCallback(FM225_FaceCountCb_t cb);

/**
 * @brief  当前是否正在录入
 * @retval true 正在录入
 */
bool FM225_IsEnrolling(void);

/**
 * @brief  获取当前活动命令（调试用）
 * @retval 当前正在等待响应的命令，FM225_CMD_NONE 表示空闲
 */
FM225_Command_t FM225_GetActiveCommand(void);

/**
 * @brief  获取上一次 REPLY 帧的原始数据（调试用）
 * @param  buf      输出缓冲区
 * @param  max_len  缓冲区最大长度
 * @retval 实际数据长度
 */
uint16_t FM225_GetLastReply(uint8_t *buf, uint16_t max_len);

#endif /* __FM225_H */
