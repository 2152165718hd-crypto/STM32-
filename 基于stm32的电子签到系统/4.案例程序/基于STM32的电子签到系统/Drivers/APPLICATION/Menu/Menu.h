#ifndef __MENU_H
#define __MENU_H

#include "stm32f1xx_hal.h"

#include "APPLICATION/Menu/MenuFramework.h"
#include "Hardware/AT24C256/AT24C256.h"
#include "Hardware/Buzzer/Buzzer.h"
#include "Hardware/DS1302_RTC/DS1302_RTC.h"
#include "Hardware/ESP_01S/ESP_01S.h"
#include "Hardware/KEY/KEY.h"
#include "Hardware/OLED/OLED.h"
#include "Hardware/RC522/RC522.h"

#define ATTENDANCE_PERSON_ID_LEN 3u
#define ATTENDANCE_PERSON_ID_STORAGE_LEN 10u
#define ATTENDANCE_MAX_AUTH_CARDS 100u
#define ATTENDANCE_MAX_RECORDS 900u

typedef enum
{
    ATTENDANCE_VIEW_HOME = 0,
    ATTENDANCE_VIEW_RECORDS,
    ATTENDANCE_VIEW_MENU
} AttendanceDisplayMode_t;

typedef enum
{
    ATTENDANCE_STATUS_WAIT_CARD = 0,
    ATTENDANCE_STATUS_SUCCESS,
    ATTENDANCE_STATUS_INVALID_CARD,
    ATTENDANCE_STATUS_TIME_INVALID,
    ATTENDANCE_STATUS_STORAGE_ERROR,
    ATTENDANCE_STATUS_WIFI_ERROR,
    ATTENDANCE_STATUS_RECORDS_CLEARED,
    ATTENDANCE_STATUS_CARD_SAVED,
    ATTENDANCE_STATUS_CARD_DELETED,
    ATTENDANCE_STATUS_CARD_NOT_FOUND,
    ATTENDANCE_STATUS_CARDS_CLEARED
} AttendanceStatus_t;

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t authorized_count;
    uint16_t record_count;
    uint16_t write_index;
    uint16_t last_index;
    uint8_t reserved[50];
} AttendanceStorageHeader_t;

typedef struct
{
    uint8_t valid;
    uint8_t uid_len;
    uint8_t uid[RC522_UID_MAX_LEN];
    char person_id[ATTENDANCE_PERSON_ID_STORAGE_LEN + 1u];
    uint8_t reserved[9];
} AttendanceAuthorizedCard_t;

typedef struct
{
    uint8_t valid;
    uint8_t uid_len;
    uint8_t uid[RC522_UID_MAX_LEN];
    char person_id[ATTENDANCE_PERSON_ID_STORAGE_LEN + 1u];
    uint8_t year;
    uint8_t month;
    uint8_t date;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t reserved[2];
} AttendanceRecord_t;

void Attendance_AppInit(void);
void Attendance_AppLoop(void);

#endif
