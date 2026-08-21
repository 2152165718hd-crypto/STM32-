#ifndef __FM225_H
#define __FM225_H

#include "stm32f4xx_hal.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Hardware config */
#define FM225_TX_PIN GPIO_PIN_12
#define FM225_TX_GPIO_PORT GPIOC
#define FM225_RX_PIN GPIO_PIN_2
#define FM225_RX_GPIO_PORT GPIOD
#define FM225_UART UART5
#define FM225_UART_BAUD 115200
#define FM225_UART_IRQn UART5_IRQn
#define FM225_UART_CLK_ENABLE() __HAL_RCC_UART5_CLK_ENABLE()
#define FM225_TX_GPIO_CLK_ENABLE() __HAL_RCC_GPIOC_CLK_ENABLE()
#define FM225_RX_GPIO_CLK_ENABLE() __HAL_RCC_GPIOD_CLK_ENABLE()
#define FM225_UART_GPIO_AF GPIO_AF8_UART5

/* Protocol limits */
#define FM225_START_CODE 0xEFAA
#define FM225_NAME_SIZE 32
#define FM225_MAX_RESPONSE_SIZE 256
#define FM225_RX_BUF_SIZE 512
#define FM225_CMD_TIMEOUT_MS 15000U
#define FM225_RESET_TIMEOUT_MS 5000U
#define FM225_VERIFY_TIMEOUT_S 10U
#define FM225_ENROLL_CAPTURE_TIMEOUT_S 20U
#define FM225_ENROLL_TIMEOUT_MS 25000U
#define FM225_RX_STALE_TIMEOUT_MS 200U

typedef enum
{
    FM225_CMD_NONE = 0x00,
    FM225_CMD_RESET = 0x10,
    FM225_CMD_GET_STATUS = 0x11,
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

typedef enum
{
    FM225_RESP_REPLY = 0x00,
    FM225_RESP_NOTE = 0x01,
    FM225_RESP_IMAGE = 0x02,
} FM225_ResponseType_t;

typedef enum
{
    FM225_NOTE_READY = 0x00,
    FM225_NOTE_FACE_STATE = 0x01,
} FM225_NoteType_t;

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

typedef enum
{
    FM225_DIR_UNDEFINED = 0x00,
    FM225_DIR_MIDDLE = 0x01,
    FM225_DIR_RIGHT = 0x02,
    FM225_DIR_LEFT = 0x04,
    FM225_DIR_DOWN = 0x08,
    FM225_DIR_UP = 0x10,
} FM225_FaceDirection_t;

typedef void (*FM225_MatchedCb_t)(int16_t face_id, const char *name);
typedef void (*FM225_UnmatchedCb_t)(void);
typedef void (*FM225_InvalidCb_t)(uint8_t error);
typedef void (*FM225_EnrollDoneCb_t)(int16_t face_id, uint8_t direction);
typedef void (*FM225_EnrollFailCb_t)(uint8_t error);
typedef void (*FM225_FaceInfoCb_t)(int16_t status, int16_t left, int16_t top,
                                   int16_t right, int16_t bottom,
                                   int16_t yaw, int16_t pitch, int16_t roll);
typedef void (*FM225_FaceCountCb_t)(uint8_t count);

void FM225_Init(void);
void FM225_Process(void);
void FM225_RxCallback(UART_HandleTypeDef *huart);
void FM225_ErrorCallback(UART_HandleTypeDef *huart);
UART_HandleTypeDef *FM225_GetUartHandle(void);

void FM225_EnrollFace(const char *name, FM225_FaceDirection_t direction);
void FM225_VerifyFace(void);
void FM225_DeleteFace(int16_t face_id);
void FM225_DeleteAllFaces(void);
void FM225_GetFaceCount(void);
void FM225_Reset(void);
void FM225_Abort(void);

void FM225_SetMatchedCallback(FM225_MatchedCb_t cb);
void FM225_SetUnmatchedCallback(FM225_UnmatchedCb_t cb);
void FM225_SetInvalidCallback(FM225_InvalidCb_t cb);
void FM225_SetEnrollDoneCallback(FM225_EnrollDoneCb_t cb);
void FM225_SetEnrollFailCallback(FM225_EnrollFailCb_t cb);
void FM225_SetFaceInfoCallback(FM225_FaceInfoCb_t cb);
void FM225_SetFaceCountCallback(FM225_FaceCountCb_t cb);

bool FM225_IsEnrolling(void);
FM225_Command_t FM225_GetActiveCommand(void);
uint16_t FM225_GetLastReply(uint8_t *buf, uint16_t max_len);

#endif /* __FM225_H */
