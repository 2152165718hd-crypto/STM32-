#ifndef __MENU_H
#define __MENU_H

#include "stm32f1xx_hal.h"
#include ".\APPLICATION\Menu\MenuFramework.h"
#include ".\Hardware\FM225\FM225.h"
#include ".\Hardware\ESP_01S\ESP_01S.h"
#include ".\Hardware\Buzzer\Buzzer.h"
#include ".\Hardware\Relay\Relay.h"
#include ".\SYSTEM\delay\delay.h"
#include ".\SYSTEM\sys\sys.h"
#include <stdio.h>
#include <string.h>

#define APP_UNLOCK_PULSE_MS         3000U
#define APP_ALARM_DURATION_MS      3000U
#define APP_STATUS_PUSH_INTERVAL_MS 3000U

#define APP_ESP_HISTORY_MAX         5U
#define APP_ESP_CMD_LEN             48U
#define APP_ESP_TX_LEN              80U
#define APP_OLED_PREVIEW_LEN        18U
#define APP_FACE_MAX_COUNT          100U

#ifndef APP_USE_IWDG
#define APP_USE_IWDG 1
#endif

typedef enum
{
    ENROLL_IDLE = 0,
    ENROLL_MIDDLE,
    ENROLL_RIGHT,
    ENROLL_LEFT,
    ENROLL_DOWN,
    ENROLL_UP,
    ENROLL_DONE,
    ENROLL_FAIL
} EnrollState_t;

typedef enum
{
    VERIFY_IDLE = 0,
    VERIFY_WAITING,
    VERIFY_SUCCESS,
    VERIFY_FAIL,
    VERIFY_ERROR
} VerifyState_t;

typedef enum
{
    APP_LOCK_LOCKED = 0,
    APP_LOCK_UNLOCKED
} AppLockState_t;


typedef enum
{
    APP_RESULT_IDLE = 0,
    APP_RESULT_PASS,
    APP_RESULT_FAIL,
    APP_RESULT_ERROR
} AppLastResult_t;

void App_Init(void);
void App_Loop(void);

#endif
