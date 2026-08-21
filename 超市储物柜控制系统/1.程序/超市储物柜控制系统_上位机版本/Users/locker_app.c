#include "locker_app.h"
#include "app_ui_font.h"
#include "HARDWARE/AT24C256/AT24C256.h"
#include "HARDWARE/Buzzer/Buzzer.h"
#include "HARDWARE/ESP_01S/ESP_01S.h"
#include "HARDWARE/FM225/FM225.h"
#include "HARDWARE/Independent_Key/KEY.h"
#include "HARDWARE/Matrix_Keypad/Matrix_Keypad.h"
#include "HARDWARE/PCF8563/PCF8563.h"
#include "HARDWARE/Relay/Relay.h"
#include "HARDWARE/TFT_ST7735/TFT_ST7735.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define LOCKER_COUNT 16U
#define LOCKER_PASSWORD_LEN 6U
#define LOCKER_RECORD_COUNT 64U
#define LOCKER_STORAGE_ADDR 0U
#define LOCKER_STORAGE_MAGIC 0x4C4F434BU
#define LOCKER_STORAGE_VERSION 2U
#define LOCKER_DOOR_TIMEOUT_MS 30000U
#define LOCKER_STATUS_INTERVAL_MS 5000U

#define TCP_PROTOCOL_VERSION 1U
#define TCP_LINE_MAX 256U
#define TCP_CMD_QUEUE_SIZE 8U
#define TCP_BINARY_SOF_0 0xAAU
#define TCP_BINARY_SOF_1 0x55U
#define TCP_BINARY_EOF_0 0x55U
#define TCP_BINARY_EOF_1 0xAAU
#define TCP_BINARY_VERSION 1U
#define TCP_BINARY_DEVICE_ADDR_DEFAULT 0x01U
#define TCP_BINARY_MAX_PAYLOAD 256U
#define TCP_BINARY_CMD_HEARTBEAT 0x01U
#define TCP_BINARY_CMD_STATUS_QUERY 0x10U
#define TCP_BINARY_CMD_LOCKER_CONTROL 0x20U
#define TCP_BINARY_CMD_CLEAR_ALARM 0x30U
#define TCP_BINARY_CMD_EMERGENCY_UNLOCK 0x31U
#define TCP_BINARY_CMD_RECORD_EXPORT 0x40U
#define TCP_BINARY_CMD_SET_TIME 0x50U
#define TCP_BINARY_RESPONSE_MASK 0x80U
#define TCP_BINARY_RESPONSE_CACHE_SIZE 8U

#define LOCKER_ACTION_OPEN 1U
#define LOCKER_ACTION_CLOSE 2U
#define LOCKER_ACTION_TOGGLE 3U
#define LOCKER_ACTION_FORCE_OPEN 4U

#define APP_REDRAW_FULL 0x0001U
#define APP_REDRAW_HOME_STATUS 0x0002U
#define APP_REDRAW_MENU_SELECT 0x0004U
#define APP_REDRAW_INPUT_VALUE 0x0008U
#define APP_REDRAW_DOOR_TIMER 0x0010U
#define APP_REDRAW_DOOR_ALARM 0x0020U
#define APP_REDRAW_ADMIN_LOCKER 0x0040U
#define APP_REDRAW_FACE_UI 0x0080U

#define APP_FACE_STATUS_UNKNOWN ((int16_t)-32768)
#define APP_FACE_ANIM_INTERVAL_MS 120U

#define TXT_SUPERMARKET_LOCKER "\xE8""\xB6""\x85""\xE5""\xB8""\x82""\xE5""\x82""\xA8""\xE7""\x89""\xA9""\xE6""\x9F""\x9C"
#define TXT_STORE "\xE5""\xAD""\x98""\xE7""\x89""\xA9"
#define TXT_TAKE "\xE5""\x8F""\x96""\xE7""\x89""\xA9"
#define TXT_ADMIN "\xE7""\xAE""\xA1""\xE7""\x90""\x86""\xE5""\x91""\x98"
#define TXT_RECORD_SYSTEM "\xE8""\xAE""\xB0""\xE5""\xBD""\x95""/""\xE7""\xB3""\xBB""\xE7""\xBB""\x9F"
#define TXT_SYSTEM "\xE7""\xB3""\xBB""\xE7""\xBB""\x9F"
#define TXT_OPEN_DOOR "\xE5""\xBC""\x80""\xE9""\x97""\xA8"
#define TXT_CLOSE_DOOR "\xE5""\x85""\xB3""\xE9""\x97""\xA8"
#define TXT_PASSWORD "\xE5""\xAF""\x86""\xE7""\xA0""\x81"
#define TXT_ALARM "\xE6""\x8A""\xA5""\xE8""\xAD""\xA6"

typedef enum
{
    PAGE_HOME = 0,
    PAGE_MENU,
    PAGE_STORE_PASS,
    PAGE_STORE_FACE,
    PAGE_TAKE_FACE,
    PAGE_TAKE_CABINET,
    PAGE_TAKE_PASS,
    PAGE_ADMIN_PASS,
    PAGE_ADMIN_MENU,
    PAGE_ADMIN_OPEN,
    PAGE_RECORDS,
    PAGE_DOOR_OPEN,
    PAGE_MESSAGE
} AppPage_t;

typedef enum
{
    OPEN_NONE = 0,
    OPEN_STORE,
    OPEN_TAKE,
    OPEN_ADMIN
} DoorOpenMode_t;

typedef enum
{
    TCP_TRANSPORT_NONE = 0,
    TCP_TRANSPORT_JSON,
    TCP_TRANSPORT_BINARY
} TcpTransportMode_t;

typedef enum
{
    TCP_CMD_NONE = 0,
    TCP_CMD_HEARTBEAT,
    TCP_CMD_GET_STATUS,
    TCP_CMD_OPEN_LOCKER,
    TCP_CMD_LOCKER_CONTROL,
    TCP_CMD_CLEAR_ALARM,
    TCP_CMD_EMERGENCY_UNLOCK,
    TCP_CMD_EXPORT_RECORDS,
    TCP_CMD_SET_TIME,
    TCP_CMD_ERROR
} TcpCommandType_t;

typedef enum
{
    TCP_ERR_NONE = 0,
    TCP_ERR_BAD_JSON,
    TCP_ERR_INVALID_TYPE,
    TCP_ERR_UNKNOWN_CMD,
    TCP_ERR_INVALID_LOCKER,
    TCP_ERR_INVALID_ACTION,
    TCP_ERR_LINE_TOO_LONG,
    TCP_ERR_QUEUE_FULL,
    TCP_ERR_CRC_ERROR,
    TCP_ERR_BUSY,
    TCP_ERR_TIMEOUT,
    TCP_ERR_UNSUPPORTED
} TcpError_t;

typedef enum
{
    TCP_BIN_RESULT_OK = 0U,
    TCP_BIN_RESULT_BAD_REQUEST = 1U,
    TCP_BIN_RESULT_INVALID_LOCKER = 2U,
    TCP_BIN_RESULT_INVALID_ACTION = 3U,
    TCP_BIN_RESULT_BUSY = 4U,
    TCP_BIN_RESULT_UNSUPPORTED = 5U,
    TCP_BIN_RESULT_TIMEOUT = 6U,
    TCP_BIN_RESULT_CRC_ERROR = 7U
} TcpBinaryResult_t;

typedef struct
{
    TcpCommandType_t type;
    TcpError_t error;
    uint16_t seq;
    uint8_t has_seq;
    uint8_t wire_cmd;
    uint16_t locker_mask;
    uint8_t action;
    uint8_t flags;
    uint8_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t has_time;
} TcpCommand_t;

typedef struct
{
    uint8_t valid;
    uint16_t seq;
    TcpCommandType_t request_type;
    uint8_t response_cmd;
    uint16_t frame_len;
    uint8_t frame[280U];
} TcpBinaryResponseCacheEntry_t;

typedef enum
{
    TCP_PARSE_TEXT = 0,
    TCP_PARSE_SOF_2,
    TCP_PARSE_BINARY_HEADER,
    TCP_PARSE_BINARY_PAYLOAD,
    TCP_PARSE_BINARY_CRC,
    TCP_PARSE_BINARY_TAIL
} TcpParseState_t;

typedef struct
{
    uint8_t occupied;
    char password[LOCKER_PASSWORD_LEN + 1U];
    int16_t face_id;
    uint8_t reserved[3];
} LockerSlot_t;

typedef struct
{
    uint8_t type;
    uint8_t locker_id;
    int16_t face_id;
    uint8_t result;
    char timestamp[20];
} LockerRecord_t;

typedef struct
{
    uint32_t magic;
    uint16_t version;
    char admin_password[LOCKER_PASSWORD_LEN + 1U];
    uint8_t alarm;
    uint16_t record_head;
    uint16_t record_count;
    LockerSlot_t slots[LOCKER_COUNT];
    LockerRecord_t records[LOCKER_RECORD_COUNT];
    uint16_t crc;
} LockerStorage_t;

static LockerStorage_t s_store;
static AppPage_t s_page = PAGE_HOME;
static AppPage_t s_messageNext = PAGE_HOME;
static DoorOpenMode_t s_openMode = OPEN_NONE;
static uint16_t s_redrawFlags = APP_REDRAW_FULL;
static AppPage_t s_doorReturn = PAGE_HOME;
static uint8_t s_menuIndex = 0U;
static uint8_t s_prevMenuIndex = 0U;
static uint8_t s_adminIndex = 0U;
static uint8_t s_prevAdminIndex = 0U;
static AppPage_t s_recordsReturn = PAGE_HOME;
static uint8_t s_selectedLocker = 1U;
static uint8_t s_pendingLocker = 0U;
static uint8_t s_doorOpen = 0U;
static uint32_t s_doorOpenTick = 0U;
static uint32_t s_lastStatusTick = 0U;
static uint32_t s_lastHomeTick = 0U;
static uint32_t s_messageUntil = 0U;
static char s_input[10];
static uint8_t s_inputLen = 0U;
static char s_message[40];
static uint8_t s_tcpHelloSent = 0U;
static TcpTransportMode_t s_tcpTransport = TCP_TRANSPORT_NONE;
static uint16_t s_tcpResponseSeq = 0U;
static uint8_t s_tcpResponseHasSeq = 0U;
static TcpCommandType_t s_tcpResponseCmd = TCP_CMD_GET_STATUS;
static uint8_t s_exportActive = 0U;
static uint16_t s_exportIndex = 0U;
static uint16_t s_exportSeq = 0U;
static uint8_t s_exportSeqValid = 0U;
static uint8_t s_localExportRequest = 0U;
static uint8_t s_statusPushPending = 0U;
static uint16_t s_relayMask = 0U;
static char s_tcpLine[TCP_LINE_MAX + 1U];
static uint16_t s_tcpLineLen = 0U;
static uint8_t s_tcpDropLine = 0U;
static uint8_t s_tcpBinaryHeader[7U];
static uint8_t s_tcpBinaryHeaderLen = 0U;
static uint8_t s_tcpBinaryPayload[TCP_BINARY_MAX_PAYLOAD];
static uint16_t s_tcpBinaryPayloadLen = 0U;
static uint16_t s_tcpBinaryPayloadIndex = 0U;
static uint8_t s_tcpBinaryCrc[2U];
static uint8_t s_tcpBinaryCrcLen = 0U;
static uint8_t s_tcpBinaryTail[2U];
static uint8_t s_tcpBinaryTailLen = 0U;
static uint8_t s_tcpBinaryAddr = 0U;
static uint8_t s_tcpBinaryCmd = 0U;
static uint16_t s_tcpBinarySeq = 0U;
static TcpParseState_t s_tcpParseState = TCP_PARSE_TEXT;
static TcpBinaryResponseCacheEntry_t s_tcpBinaryResponseCache[TCP_BINARY_RESPONSE_CACHE_SIZE];
static uint8_t s_tcpBinaryResponseCacheNext = 0U;
static TcpCommand_t s_tcpCmdQueue[TCP_CMD_QUEUE_SIZE];
static uint8_t s_tcpCmdHead = 0U;
static uint8_t s_tcpCmdTail = 0U;
static uint8_t s_tcpCmdCount = 0U;
static uint8_t s_tcpQueueOverflow = 0U;

static void App_SetRelayMask(uint16_t mask);
static void App_SendHello(void);
static void App_StartRecordExport(const TcpCommand_t *cmd, uint8_t send_ack);

static volatile uint8_t s_faceMatchedFlag = 0U;
static volatile uint8_t s_faceUnmatchedFlag = 0U;
static volatile uint8_t s_faceInvalidFlag = 0U;
static volatile uint8_t s_enrollDoneFlag = 0U;
static volatile uint8_t s_enrollFailFlag = 0U;
static volatile int16_t s_faceMatchedId = -1;
static volatile int16_t s_enrollFaceId = -1;
static volatile uint8_t s_lastFaceError = 0U;
static volatile int16_t s_faceStatus = APP_FACE_STATUS_UNKNOWN;
static volatile int16_t s_faceLeft = 0;
static volatile int16_t s_faceTop = 0;
static volatile int16_t s_faceRight = 0;
static volatile int16_t s_faceBottom = 0;
static volatile int16_t s_faceYaw = 0;
static volatile int16_t s_facePitch = 0;
static volatile int16_t s_faceRoll = 0;
static uint8_t s_faceAnimFrame = 0U;
static uint32_t s_faceAnimTick = 0U;

static const char *const s_mainMenu[] = {
    TXT_STORE,
    TXT_TAKE,
    TXT_ADMIN,
    TXT_RECORD_SYSTEM,
};

static const char *const s_adminMenu[] = {
    TXT_OPEN_DOOR,
    "Status",
    "Export",
    "Clear Alarm",
    "Clear All",
};

static uint16_t App_CalcCrc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFFU;
    uint16_t i;
    uint8_t bit;

    for (i = 0U; i < len; i++)
    {
        crc ^= (uint16_t)data[i] << 8U;
        for (bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 0x8000U) != 0U)
            {
                crc = (uint16_t)((crc << 1U) ^ 0x1021U);
            }
            else
            {
                crc <<= 1U;
            }
        }
    }

    return crc;
}

static uint16_t App_StorageCrc(const LockerStorage_t *store)
{
    uint16_t len;

    if (store == NULL)
    {
        return 0U;
    }

    len = (uint16_t)((const uint8_t *)&store->crc - (const uint8_t *)store);
    return App_CalcCrc16((const uint8_t *)store, len);
}

static void App_UpdateStorageCrc(LockerStorage_t *store)
{
    if (store == NULL)
    {
        return;
    }

    store->crc = App_StorageCrc(store);
}

static uint8_t App_StorageValid(const LockerStorage_t *store)
{
    uint8_t i;

    if (store == NULL)
    {
        return 0U;
    }

    if ((store->magic != LOCKER_STORAGE_MAGIC) ||
        (store->version != LOCKER_STORAGE_VERSION) ||
        (store->record_head >= LOCKER_RECORD_COUNT) ||
        (store->record_count > LOCKER_RECORD_COUNT) ||
        (store->crc != App_StorageCrc(store)))
    {
        return 0U;
    }

    for (i = 0U; i < LOCKER_COUNT; i++)
    {
        if (store->slots[i].occupied > 1U)
        {
            return 0U;
        }
    }

    return 1U;
}

static void App_RequestRedraw(uint16_t flags)
{
    s_redrawFlags |= flags;
}

static void App_RequestStatusRedraw(void)
{
    if (s_page == PAGE_HOME)
    {
        App_RequestRedraw(APP_REDRAW_HOME_STATUS);
    }
    else if (s_page == PAGE_DOOR_OPEN)
    {
        App_RequestRedraw(APP_REDRAW_DOOR_TIMER | APP_REDRAW_DOOR_ALARM);
    }
    else
    {
        App_RequestRedraw(APP_REDRAW_FULL);
    }
}

static void App_RequestNetworkStatus(void)
{
    s_statusPushPending = 1U;
}

static void App_SetPage(AppPage_t page)
{
    s_page = page;
    s_redrawFlags = APP_REDRAW_FULL;
}

static void App_ClearInput(void)
{
    memset(s_input, 0, sizeof(s_input));
    s_inputLen = 0U;
}

static void App_ShowMessage(const char *message, AppPage_t next, uint16_t ms)
{
    strncpy(s_message, message, sizeof(s_message) - 1U);
    s_message[sizeof(s_message) - 1U] = '\0';
    s_messageNext = next;
    s_messageUntil = HAL_GetTick() + ms;
    App_SetPage(PAGE_MESSAGE);
}

static uint8_t App_CountUsed(void)
{
    uint8_t i;
    uint8_t used = 0U;

    for (i = 0U; i < LOCKER_COUNT; i++)
    {
        if (s_store.slots[i].occupied != 0U)
        {
            used++;
        }
    }

    return used;
}

static uint8_t App_FindEmptyLocker(void)
{
    uint8_t i;

    for (i = 0U; i < LOCKER_COUNT; i++)
    {
        if (s_store.slots[i].occupied == 0U)
        {
            return (uint8_t)(i + 1U);
        }
    }

    return 0U;
}

static uint8_t App_FindLockerByFace(int16_t face_id)
{
    uint8_t i;

    for (i = 0U; i < LOCKER_COUNT; i++)
    {
        if ((s_store.slots[i].occupied != 0U) && (s_store.slots[i].face_id == face_id))
        {
            return (uint8_t)(i + 1U);
        }
    }

    return 0U;
}

static void App_FormatTime(char *buf, uint16_t len)
{
    PCF8563_Time_t now;

    if ((buf == NULL) || (len == 0U))
    {
        return;
    }

    if ((PCF8563_GetTime(&now) == HAL_OK) && (now.valid != 0U))
    {
        (void)snprintf(buf, len, "20%02u-%02u-%02u %02u:%02u:%02u",
                   now.year, now.month, now.day, now.hour, now.minute, now.second);
    }
    else
    {
        (void)snprintf(buf, len, "RTC_ERR");
    }
}

static uint8_t App_ParseBuildMonth(const char *month)
{
    if (strncmp(month, "Jan", 3) == 0)
    {
        return 1U;
    }
    if (strncmp(month, "Feb", 3) == 0)
    {
        return 2U;
    }
    if (strncmp(month, "Mar", 3) == 0)
    {
        return 3U;
    }
    if (strncmp(month, "Apr", 3) == 0)
    {
        return 4U;
    }
    if (strncmp(month, "May", 3) == 0)
    {
        return 5U;
    }
    if (strncmp(month, "Jun", 3) == 0)
    {
        return 6U;
    }
    if (strncmp(month, "Jul", 3) == 0)
    {
        return 7U;
    }
    if (strncmp(month, "Aug", 3) == 0)
    {
        return 8U;
    }
    if (strncmp(month, "Sep", 3) == 0)
    {
        return 9U;
    }
    if (strncmp(month, "Oct", 3) == 0)
    {
        return 10U;
    }
    if (strncmp(month, "Nov", 3) == 0)
    {
        return 11U;
    }
    if (strncmp(month, "Dec", 3) == 0)
    {
        return 12U;
    }

    return 1U;
}

static uint8_t App_CalcWeekday(uint16_t year, uint8_t month, uint8_t day)
{
    static const uint8_t table[] = {0U, 3U, 2U, 5U, 0U, 3U, 5U, 1U, 4U, 6U, 2U, 4U};

    if (month < 3U)
    {
        year = (uint16_t)(year - 1U);
    }

    return (uint8_t)((year + (year / 4U) - (year / 100U) + (year / 400U) + table[month - 1U] + day) % 7U);
}

static void App_FillTimeFromBuild(PCF8563_Time_t *time)
{
    const char *date = __DATE__;
    const char *clock = __TIME__;
    uint16_t year = (uint16_t)((date[7] - '0') * 1000 + (date[8] - '0') * 100 + (date[9] - '0') * 10 + (date[10] - '0'));
    uint8_t month = App_ParseBuildMonth(date);
    uint8_t day = (uint8_t)((date[4] == ' ') ? (date[5] - '0') : ((date[4] - '0') * 10 + (date[5] - '0')));
    uint8_t hour = (uint8_t)((clock[0] - '0') * 10 + (clock[1] - '0'));
    uint8_t minute = (uint8_t)((clock[3] - '0') * 10 + (clock[4] - '0'));
    uint8_t second = (uint8_t)((clock[6] - '0') * 10 + (clock[7] - '0'));

    if (time == NULL)
    {
        return;
    }

    time->year = (uint8_t)(year % 100U);
    time->month = month;
    time->day = day;
    time->weekday = App_CalcWeekday(year, month, day);
    time->hour = hour;
    time->minute = minute;
    time->second = second;
    time->valid = 1U;
}

static void App_LoadDefaults(void)
{
    memset(&s_store, 0, sizeof(s_store));
    s_store.magic = LOCKER_STORAGE_MAGIC;
    s_store.version = LOCKER_STORAGE_VERSION;
    strcpy(s_store.admin_password, "123456");
    App_UpdateStorageCrc(&s_store);
}

static void App_SaveStorage(void)
{
    App_UpdateStorageCrc(&s_store);
    (void)AT24C256_Write(LOCKER_STORAGE_ADDR, (const uint8_t *)&s_store, sizeof(s_store));
}

static void App_LoadStorage(void)
{
    if ((AT24C256_Read(LOCKER_STORAGE_ADDR, (uint8_t *)&s_store, sizeof(s_store)) != HAL_OK) ||
        (App_StorageValid(&s_store) == 0U))
    {
        App_LoadDefaults();
        App_SaveStorage();
    }
}

static void App_AddRecord(uint8_t type, uint8_t locker_id, int16_t face_id, uint8_t result)
{
    LockerRecord_t *rec = &s_store.records[s_store.record_head % LOCKER_RECORD_COUNT];

    memset(rec, 0, sizeof(*rec));
    rec->type = type;
    rec->locker_id = locker_id;
    rec->face_id = face_id;
    rec->result = result;
    App_FormatTime(rec->timestamp, sizeof(rec->timestamp));

    s_store.record_head = (uint16_t)((s_store.record_head + 1U) % LOCKER_RECORD_COUNT);
    if (s_store.record_count < LOCKER_RECORD_COUNT)
    {
        s_store.record_count++;
    }
    App_SaveStorage();
}

static void App_DrawHeader(const char *title)
{
    ST7735_DrawRectangle(0U, 0U, 128U, 20U, ST7735_BLUE);
    AppFont_DrawText(4U, 2U, title, ST7735_WHITE, ST7735_BLUE);
}

static void App_DrawFooter(const char *text)
{
    ST7735_DrawRectangle(0U, 146U, 128U, 14U, ST7735_BLACK);
    ST7735_DrawString(2U, 148U, text, ST7735_WHITE, ST7735_BLACK, &Font_7x10);
}

static void App_DrawLockerGrid(uint16_t y)
{
    uint8_t i;

    for (i = 0U; i < LOCKER_COUNT; i++)
    {
        uint16_t x = (uint16_t)(4U + (i % 4U) * 31U);
        uint16_t yy = (uint16_t)(y + (i / 4U) * 20U);
        uint16_t color = (s_store.slots[i].occupied != 0U) ? ST7735_RED : ST7735_GREEN;
        char num[4];

        ST7735_DrawRectangle(x, yy, 28U, 17U, color);
        (void)snprintf(num, sizeof(num), "%02u", (unsigned int)(i + 1U));
        ST7735_DrawString((uint16_t)(x + 6U), (uint16_t)(yy + 4U), num, ST7735_WHITE, color, &Font_7x10);
    }
}

static void App_DrawHomeStatus(void)
{
    char line[32];
    PCF8563_Time_t now;

    ST7735_DrawRectangle(0U, 24U, 128U, 40U, ST7735_BLACK);
    if ((PCF8563_GetTime(&now) == HAL_OK) && (now.valid != 0U))
    {
        (void)snprintf(line, sizeof(line), "20%02u-%02u-%02u",
                       now.year, now.month, now.day);
        ST7735_DrawString(2U, 24U, line, ST7735_CYAN, ST7735_BLACK, &Font_7x10);
        (void)snprintf(line, sizeof(line), "%02u:%02u:%02u",
                       now.hour, now.minute, now.second);
        ST7735_DrawString(2U, 34U, line, ST7735_CYAN, ST7735_BLACK, &Font_7x10);
    }
    else
    {
        ST7735_DrawString(2U, 24U, "RTC_ERR", ST7735_CYAN, ST7735_BLACK, &Font_7x10);
        ST7735_DrawString(2U, 34U, "--:--:--", ST7735_CYAN, ST7735_BLACK, &Font_7x10);
    }
    (void)snprintf(line, sizeof(line), "AP:%s CL:%s AL:%s",
                   ESP_IsWifiConnected() ? "ON" : "--",
                   ESP_IsConnected() ? "ON" : "--",
                   s_store.alarm ? "ON" : "OFF");
    ST7735_DrawString(2U, 44U, line, s_store.alarm ? ST7735_RED : ST7735_WHITE, ST7735_BLACK, &Font_7x10);
    (void)snprintf(line, sizeof(line), "USED:%u/%u", (unsigned int)App_CountUsed(), (unsigned int)LOCKER_COUNT);
    ST7735_DrawString(2U, 54U, line, ST7735_YELLOW, ST7735_BLACK, &Font_7x10);
}

static void App_DrawHome(void)
{
    ST7735_FillScreen(ST7735_BLACK);
    App_DrawHeader(TXT_SUPERMARKET_LOCKER);
    App_DrawHomeStatus();
    App_DrawLockerGrid(64U);
    App_DrawFooter("ENTER:Menu");
}

static void App_DrawMenuItem(const char *item, uint8_t index, uint8_t selected)
{
    uint16_t bg = selected ? ST7735_BLUE : ST7735_BLACK;
    uint16_t fg = selected ? ST7735_WHITE : ST7735_CYAN;

    ST7735_DrawRectangle(0U, (uint16_t)(26U + index * 24U), 128U, 20U, bg);
    AppFont_DrawText(10U, (uint16_t)(28U + index * 24U), item, fg, bg);
}

static void App_DrawMenuSelection(const char *const *items, uint8_t previous, uint8_t current)
{
    if (previous != current)
    {
        App_DrawMenuItem(items[previous], previous, 0U);
    }
    App_DrawMenuItem(items[current], current, 1U);
}

static void App_DrawMenu(const char *title, const char *const *items, uint8_t count, uint8_t index)
{
    uint8_t i;

    ST7735_FillScreen(ST7735_BLACK);
    App_DrawHeader(title);

    for (i = 0U; i < count; i++)
    {
        App_DrawMenuItem(items[i], i, (i == index) ? 1U : 0U);
    }

    App_DrawFooter("UP/DN ENTER BACK");
}

static void App_DrawInputValue(uint8_t hide)
{
    char stars[10];
    uint8_t i;

    memset(stars, 0, sizeof(stars));
    for (i = 0U; i < s_inputLen; i++)
    {
        stars[i] = hide ? '*' : s_input[i];
    }
    ST7735_DrawRectangle(8U, 60U, 112U, 26U, ST7735_BLUE);
    ST7735_DrawString(18U, 66U, stars, ST7735_WHITE, ST7735_BLUE, &Font_11x18);
}

static void App_DrawInput(const char *title, const char *hint, uint8_t hide)
{
    ST7735_FillScreen(ST7735_BLACK);
    App_DrawHeader(title);
    ST7735_DrawString(4U, 34U, hint, ST7735_WHITE, ST7735_BLACK, &Font_7x10);
    App_DrawInputValue(hide);
    App_DrawFooter("*:Del #:OK BACK");
}

static void App_DrawDoorTimer(void)
{
    char line[28];
    uint32_t elapsed = HAL_GetTick() - s_doorOpenTick;
    uint32_t remain = (elapsed >= LOCKER_DOOR_TIMEOUT_MS) ? 0U : ((LOCKER_DOOR_TIMEOUT_MS - elapsed) / 1000U);

    ST7735_DrawRectangle(0U, 72U, 128U, 16U, ST7735_BLACK);
    (void)snprintf(line, sizeof(line), "Close in:%lus", (unsigned long)remain);
    ST7735_DrawString(16U, 74U, line, ST7735_WHITE, ST7735_BLACK, &Font_7x10);
}

static void App_DrawDoorAlarm(void)
{
    ST7735_DrawRectangle(0U, 96U, 128U, 24U, ST7735_BLACK);
    if (s_store.alarm != 0U)
    {
        AppFont_DrawText(36U, 100U, TXT_ALARM, ST7735_RED, ST7735_BLACK);
    }
}

static void App_DrawDoorOpen(void)
{
    char line[28];

    ST7735_FillScreen(ST7735_BLACK);
    App_DrawHeader(TXT_OPEN_DOOR);
    (void)snprintf(line, sizeof(line), "Locker:%02u", (unsigned int)s_selectedLocker);
    ST7735_DrawString(18U, 46U, line, ST7735_YELLOW, ST7735_BLACK, &Font_11x18);
    App_DrawDoorTimer();
    App_DrawDoorAlarm();
    App_DrawFooter("ENTER:Close");
}

static void App_DrawAdminOpenLocker(void)
{
    char line[24];

    ST7735_DrawRectangle(0U, 52U, 128U, 24U, ST7735_BLACK);
    (void)snprintf(line, sizeof(line), "Locker:%02u", (unsigned int)s_selectedLocker);
    ST7735_DrawString(20U, 54U, line, ST7735_YELLOW, ST7735_BLACK, &Font_11x18);
}

static void App_DrawRecords(void)
{
    char line[32];

    ST7735_FillScreen(ST7735_BLACK);
    App_DrawHeader(TXT_RECORD_SYSTEM);
    (void)snprintf(line, sizeof(line), "Records:%u", (unsigned int)s_store.record_count);
    ST7735_DrawString(4U, 34U, line, ST7735_WHITE, ST7735_BLACK, &Font_7x10);
    (void)snprintf(line, sizeof(line), "AP:%s CL:%s",
                   ESP_IsWifiConnected() ? "ON" : "--",
                   ESP_IsConnected() ? "ON" : "--");
    ST7735_DrawString(4U, 48U, line,
                      ESP_IsWifiConnected() ? ST7735_GREEN : ST7735_RED,
                      ST7735_BLACK, &Font_7x10);
    (void)snprintf(line, sizeof(line), "Used:%u Free:%u",
                   (unsigned int)App_CountUsed(), (unsigned int)(LOCKER_COUNT - App_CountUsed()));
    ST7735_DrawString(4U, 62U, line, ST7735_CYAN, ST7735_BLACK, &Font_7x10);
    App_DrawLockerGrid(82U);
    App_DrawFooter("ENTER:Export BACK");
}

static void App_DrawMessage(void)
{
    ST7735_FillScreen(ST7735_BLACK);
    App_DrawHeader(TXT_SYSTEM);
    ST7735_DrawString(8U, 58U, s_message, ST7735_WHITE, ST7735_BLACK, &Font_7x10);
    App_DrawFooter("BACK");
}

static void App_ResetFaceUi(void)
{
    s_faceStatus = APP_FACE_STATUS_UNKNOWN;
    s_faceLeft = 0;
    s_faceTop = 0;
    s_faceRight = 0;
    s_faceBottom = 0;
    s_faceYaw = 0;
    s_facePitch = 0;
    s_faceRoll = 0;
    s_faceAnimFrame = 0U;
    s_faceAnimTick = HAL_GetTick();
}

static const char *App_GetFaceErrorText(uint8_t error)
{
    switch (error)
    {
    case FM225_REJECTED:
        return "not matched";
    case FM225_ERR_CAMERA:
        return "camera err";
    case FM225_ERR_FACE_ENROLLED:
        return "already saved";
    case FM225_ERR_LIVENESS:
        return "liveness fail";
    case FM225_ERR_TIMEOUT:
        return "timeout";
    case FM225_ERR_NO_RGBIMAGE:
        return "image lost";
    case FM225_ERR_JPG_LARGE:
        return "too close";
    case FM225_ERR_JPG_SMALL:
        return "too far";
    default:
        return "module err";
    }
}

static void App_BuildFaceHint(uint8_t enrolling, char *line1, uint16_t line1_len,
                              char *line2, uint16_t line2_len, uint16_t *line1_color)
{
    int16_t center_x;
    int16_t center_y;
    int16_t width;
    int16_t height;

    if ((line1 == NULL) || (line2 == NULL) || (line1_len == 0U) || (line2_len == 0U))
    {
        return;
    }

    *line1_color = ST7735_WHITE;
    (void)snprintf(line1, line1_len, "%s", enrolling ? "Look at camera" : "Face verify");
    (void)snprintf(line2, line2_len, "%s", enrolling ? "Keep 25-40cm away" : "BACK -> password");

    if (s_faceStatus == 1)
    {
        *line1_color = ST7735_YELLOW;
        (void)snprintf(line1, line1_len, "No face detected");
        (void)snprintf(line2, line2_len, "Move into frame");
        return;
    }

    if (s_faceStatus != 0)
    {
        return;
    }

    center_x = (int16_t)((s_faceLeft + s_faceRight) / 2);
    center_y = (int16_t)((s_faceTop + s_faceBottom) / 2);
    width = (int16_t)(s_faceRight - s_faceLeft);
    height = (int16_t)(s_faceBottom - s_faceTop);

    *line1_color = ST7735_GREEN;

    if ((width < 130) || (height < 150))
    {
        (void)snprintf(line1, line1_len, "Move closer");
        (void)snprintf(line2, line2_len, "Face is too small");
    }
    else if (center_x < 190)
    {
        (void)snprintf(line1, line1_len, "Move left");
        (void)snprintf(line2, line2_len, "Center your face");
    }
    else if (center_x > 290)
    {
        (void)snprintf(line1, line1_len, "Move right");
        (void)snprintf(line2, line2_len, "Center your face");
    }
    else if (center_y < 170)
    {
        (void)snprintf(line1, line1_len, "Move down");
        (void)snprintf(line2, line2_len, "Face is too high");
    }
    else if (center_y > 320)
    {
        (void)snprintf(line1, line1_len, "Move up");
        (void)snprintf(line2, line2_len, "Face is too low");
    }
    else if (s_faceYaw > 12)
    {
        (void)snprintf(line1, line1_len, "Turn left");
        (void)snprintf(line2, line2_len, "Keep both eyes visible");
    }
    else if (s_faceYaw < -12)
    {
        (void)snprintf(line1, line1_len, "Turn right");
        (void)snprintf(line2, line2_len, "Keep both eyes visible");
    }
    else if (s_facePitch > 10)
    {
        (void)snprintf(line1, line1_len, "Raise chin");
        (void)snprintf(line2, line2_len, "Hold steady");
    }
    else if (s_facePitch < -10)
    {
        (void)snprintf(line1, line1_len, "Lower chin");
        (void)snprintf(line2, line2_len, "Hold steady");
    }
    else
    {
        (void)snprintf(line1, line1_len, "%s", enrolling ? "Good position" : "Hold steady");
        (void)snprintf(line2, line2_len, "Scanning...");
    }
}

static void App_DrawFaceScanner(uint8_t enrolling)
{
    uint16_t frame_color = (s_faceStatus == 0) ? ST7735_GREEN : ST7735_CYAN;
    uint16_t bar_color = (s_faceStatus == 0) ? ST7735_YELLOW : ST7735_CYAN;
    uint16_t bar_y = (uint16_t)(40U + (s_faceAnimFrame % 6U) * 9U);
    uint16_t line1_color;
    uint8_t i;
    char line1[24];
    char line2[24];

    App_BuildFaceHint(enrolling, line1, sizeof(line1), line2, sizeof(line2), &line1_color);

    ST7735_DrawRectangle(0U, 24U, 128U, 122U, ST7735_BLACK);
    ST7735_DrawString(28U, 26U, enrolling ? "Face enroll" : "Face verify", ST7735_WHITE, ST7735_BLACK, &Font_7x10);

    ST7735_DrawRectangle(18U, 38U, 92U, 64U, frame_color);
    ST7735_DrawRectangle(20U, 40U, 88U, 60U, ST7735_BLACK);
    ST7735_DrawRectangle(24U, bar_y, 80U, 3U, bar_color);

    for (i = 0U; i < 4U; i++)
    {
        uint16_t dot_color = (i == (s_faceAnimFrame % 4U)) ? ST7735_YELLOW : ST7735_BLUE;
        ST7735_DrawRectangle((uint16_t)(40U + i * 12U), 108U, 6U, 6U, dot_color);
    }

    ST7735_DrawString(10U, 118U, line1, line1_color, ST7735_BLACK, &Font_7x10);
    ST7735_DrawString(8U, 130U, line2, ST7735_WHITE, ST7735_BLACK, &Font_7x10);
}

static void App_DrawFacePage(uint8_t enrolling)
{
    ST7735_FillScreen(ST7735_BLACK);
    App_DrawHeader(enrolling ? TXT_STORE : TXT_TAKE);
    App_DrawFaceScanner(enrolling);
    App_DrawFooter(enrolling ? "BACK:Cancel" : "BACK:Pwd");
}

static void App_ProcessFaceUi(uint32_t now)
{
    if ((s_page != PAGE_STORE_FACE) && (s_page != PAGE_TAKE_FACE))
    {
        return;
    }

    if ((now - s_faceAnimTick) >= APP_FACE_ANIM_INTERVAL_MS)
    {
        s_faceAnimTick = now;
        s_faceAnimFrame = (uint8_t)((s_faceAnimFrame + 1U) % 6U);
        App_RequestRedraw(APP_REDRAW_FACE_UI);
    }
}

static void App_Draw(void)
{
    uint16_t flags = s_redrawFlags;

    if (flags == 0U)
    {
        return;
    }

    s_redrawFlags = 0U;

    if ((flags & APP_REDRAW_FULL) != 0U)
    {
        switch (s_page)
        {
        case PAGE_HOME:
            App_DrawHome();
            break;
        case PAGE_MENU:
            App_DrawMenu(TXT_SYSTEM, s_mainMenu, (uint8_t)(sizeof(s_mainMenu) / sizeof(s_mainMenu[0])), s_menuIndex);
            s_prevMenuIndex = s_menuIndex;
            break;
        case PAGE_STORE_PASS:
            App_DrawInput(TXT_STORE, "Input 6 digit pass", 0U);
            break;
        case PAGE_STORE_FACE:
            App_DrawFacePage(1U);
            break;
        case PAGE_TAKE_FACE:
            App_DrawFacePage(0U);
            break;
        case PAGE_TAKE_CABINET:
            App_DrawInput(TXT_TAKE, "Input locker no", 0U);
            break;
        case PAGE_TAKE_PASS:
            App_DrawInput(TXT_PASSWORD, "Input 6 digit pass", 1U);
            break;
        case PAGE_ADMIN_PASS:
            App_DrawInput(TXT_ADMIN, "Input admin pass", 1U);
            break;
        case PAGE_ADMIN_MENU:
            App_DrawMenu(TXT_ADMIN, s_adminMenu, (uint8_t)(sizeof(s_adminMenu) / sizeof(s_adminMenu[0])), s_adminIndex);
            s_prevAdminIndex = s_adminIndex;
            break;
        case PAGE_ADMIN_OPEN:
            ST7735_FillScreen(ST7735_BLACK);
            App_DrawHeader(TXT_OPEN_DOOR);
            App_DrawAdminOpenLocker();
            App_DrawFooter("L/R ENTER BACK");
            break;
        case PAGE_RECORDS:
            App_DrawRecords();
            break;
        case PAGE_DOOR_OPEN:
            App_DrawDoorOpen();
            break;
        case PAGE_MESSAGE:
            App_DrawMessage();
            break;
        default:
            break;
        }
        return;
    }

    switch (s_page)
    {
    case PAGE_HOME:
        if ((flags & APP_REDRAW_HOME_STATUS) != 0U)
        {
            App_DrawHomeStatus();
        }
        break;
    case PAGE_MENU:
        if ((flags & APP_REDRAW_MENU_SELECT) != 0U)
        {
            App_DrawMenuSelection(s_mainMenu, s_prevMenuIndex, s_menuIndex);
            s_prevMenuIndex = s_menuIndex;
        }
        break;
    case PAGE_STORE_PASS:
    case PAGE_TAKE_CABINET:
        if ((flags & APP_REDRAW_INPUT_VALUE) != 0U)
        {
            App_DrawInputValue(0U);
        }
        break;
    case PAGE_TAKE_PASS:
    case PAGE_ADMIN_PASS:
        if ((flags & APP_REDRAW_INPUT_VALUE) != 0U)
        {
            App_DrawInputValue(1U);
        }
        break;
    case PAGE_ADMIN_MENU:
        if ((flags & APP_REDRAW_MENU_SELECT) != 0U)
        {
            App_DrawMenuSelection(s_adminMenu, s_prevAdminIndex, s_adminIndex);
            s_prevAdminIndex = s_adminIndex;
        }
        break;
    case PAGE_ADMIN_OPEN:
        if ((flags & APP_REDRAW_ADMIN_LOCKER) != 0U)
        {
            App_DrawAdminOpenLocker();
        }
        break;
    case PAGE_DOOR_OPEN:
        if ((flags & APP_REDRAW_DOOR_TIMER) != 0U)
        {
            App_DrawDoorTimer();
        }
        if ((flags & APP_REDRAW_DOOR_ALARM) != 0U)
        {
            App_DrawDoorAlarm();
        }
        break;
    case PAGE_STORE_FACE:
        if ((flags & APP_REDRAW_FACE_UI) != 0U)
        {
            App_DrawFaceScanner(1U);
        }
        break;
    case PAGE_TAKE_FACE:
        if ((flags & APP_REDRAW_FACE_UI) != 0U)
        {
            App_DrawFaceScanner(0U);
        }
        break;
    default:
        break;
    }
}

static uint8_t App_StartDoorOpen(uint8_t locker_id, DoorOpenMode_t mode)
{
    if ((locker_id == 0U) || (locker_id > LOCKER_COUNT))
    {
        App_ShowMessage("Bad locker", PAGE_HOME, 1500U);
        return 0U;
    }

    if (s_doorOpen != 0U)
    {
        return 0U;
    }

    if ((mode == OPEN_ADMIN) && ((s_page == PAGE_ADMIN_OPEN) || (s_page == PAGE_ADMIN_MENU)))
    {
        s_doorReturn = PAGE_ADMIN_MENU;
    }
    else
    {
        s_doorReturn = PAGE_HOME;
    }

    s_selectedLocker = locker_id;
    s_openMode = mode;
    s_doorOpenTick = HAL_GetTick();
    s_lastHomeTick = s_doorOpenTick;
    s_store.alarm = 0U;
    Buzzer_Off();
    App_SetRelayMask((uint16_t)(1U << (locker_id - 1U)));
    App_SetPage(PAGE_DOOR_OPEN);
    App_RequestNetworkStatus();
    return 1U;
}

static void App_CloseDoor(void)
{
    App_SetRelayMask(0U);
    Buzzer_Off();
    s_store.alarm = 0U;

    if (s_openMode == OPEN_TAKE)
    {
        int16_t face_id = s_store.slots[s_selectedLocker - 1U].face_id;
        if (face_id >= 0)
        {
            FM225_DeleteFace(face_id);
        }
        memset(&s_store.slots[s_selectedLocker - 1U], 0, sizeof(s_store.slots[0]));
        App_AddRecord(2U, s_selectedLocker, face_id, 1U);
    }
    else if (s_openMode == OPEN_STORE)
    {
        App_AddRecord(1U, s_selectedLocker, s_store.slots[s_selectedLocker - 1U].face_id, 1U);
    }
    else if (s_openMode == OPEN_ADMIN)
    {
        App_AddRecord(3U, s_selectedLocker, -1, 1U);
    }
    s_openMode = OPEN_NONE;
    App_ShowMessage("Door closed", s_doorReturn, 1000U);
    App_RequestNetworkStatus();
}

static void App_HandlePasswordKey(char key)
{
    if ((key >= '0') && (key <= '9') && (s_inputLen < LOCKER_PASSWORD_LEN))
    {
        s_input[s_inputLen++] = key;
        s_input[s_inputLen] = '\0';
        App_RequestRedraw(APP_REDRAW_INPUT_VALUE);
    }
    else if ((key == '*') && (s_inputLen > 0U))
    {
        s_input[--s_inputLen] = '\0';
        App_RequestRedraw(APP_REDRAW_INPUT_VALUE);
    }
}

static void App_StartStore(void)
{
    if (FM225_GetActiveCommand() != FM225_CMD_NONE)
    {
        App_ShowMessage("Face busy", PAGE_HOME, 1000U);
        return;
    }

    s_pendingLocker = App_FindEmptyLocker();
    if (s_pendingLocker == 0U)
    {
        App_ShowMessage("No free locker", PAGE_HOME, 1500U);
        return;
    }
    App_ClearInput();
    App_SetPage(PAGE_STORE_PASS);
}

static void App_ConfirmStorePassword(void)
{
    char name[16];

    if (s_inputLen != LOCKER_PASSWORD_LEN)
    {
        App_ShowMessage("Need 6 digits", PAGE_STORE_PASS, 1200U);
        return;
    }

    strncpy(s_store.slots[s_pendingLocker - 1U].password, s_input, LOCKER_PASSWORD_LEN);
    s_store.slots[s_pendingLocker - 1U].password[LOCKER_PASSWORD_LEN] = '\0';
    s_enrollDoneFlag = 0U;
    s_enrollFailFlag = 0U;
    s_lastFaceError = 0U;
    App_ResetFaceUi();
    (void)snprintf(name, sizeof(name), "L%02u", (unsigned int)s_pendingLocker);
    App_SetPage(PAGE_STORE_FACE);
    FM225_EnrollFace(name, FM225_DIR_UNDEFINED);
}

static void App_StartTake(void)
{
    if (FM225_GetActiveCommand() != FM225_CMD_NONE)
    {
        App_ShowMessage("Face busy", PAGE_HOME, 1000U);
        return;
    }

    s_faceMatchedFlag = 0U;
    s_faceUnmatchedFlag = 0U;
    s_faceInvalidFlag = 0U;
    s_faceMatchedId = -1;
    s_lastFaceError = 0U;
    App_ResetFaceUi();
    App_SetPage(PAGE_TAKE_FACE);
    FM225_VerifyFace();
}

static void App_StartTakePassword(void)
{
    App_ClearInput();
    s_pendingLocker = 0U;
    App_SetPage(PAGE_TAKE_CABINET);
}

static void App_ConfirmTakeLocker(void)
{
    uint8_t locker_id = (uint8_t)atoi(s_input);

    if ((locker_id == 0U) || (locker_id > LOCKER_COUNT) || (s_store.slots[locker_id - 1U].occupied == 0U))
    {
        App_ClearInput();
        App_ShowMessage("Bad locker", PAGE_TAKE_CABINET, 1200U);
        return;
    }

    s_pendingLocker = locker_id;
    App_ClearInput();
    App_SetPage(PAGE_TAKE_PASS);
}

static void App_ConfirmTakePassword(void)
{
    if ((s_inputLen == LOCKER_PASSWORD_LEN) &&
        (strncmp(s_store.slots[s_pendingLocker - 1U].password, s_input, LOCKER_PASSWORD_LEN) == 0))
    {
        s_faceMatchedId = s_store.slots[s_pendingLocker - 1U].face_id;
        App_StartDoorOpen(s_pendingLocker, OPEN_TAKE);
    }
    else
    {
        App_AddRecord(2U, s_pendingLocker, -1, 0U);
        App_ClearInput();
        App_ShowMessage("Wrong pass", PAGE_TAKE_CABINET, 1500U);
    }
}

static void App_ConfirmAdminPassword(void)
{
    if ((s_inputLen == LOCKER_PASSWORD_LEN) &&
        (strncmp(s_store.admin_password, s_input, LOCKER_PASSWORD_LEN) == 0))
    {
        s_adminIndex = 0U;
        App_SetPage(PAGE_ADMIN_MENU);
    }
    else
    {
        App_ShowMessage("Wrong pass", PAGE_HOME, 1500U);
    }
}

static void App_ClearAllLockers(void)
{
    uint8_t i;

    if (FM225_GetActiveCommand() != FM225_CMD_NONE)
    {
        App_ShowMessage("Face busy", PAGE_ADMIN_MENU, 1000U);
        return;
    }

    for (i = 0U; i < LOCKER_COUNT; i++)
    {
        memset(&s_store.slots[i], 0, sizeof(s_store.slots[i]));
    }
    App_SetRelayMask(0U);
    FM225_DeleteAllFaces();
    s_store.alarm = 0U;
    App_AddRecord(4U, 0U, -1, 1U);
    App_RequestNetworkStatus();
    App_ShowMessage("All clear", PAGE_ADMIN_MENU, 1200U);
}

static const char *App_SkipJsonSpace(const char *p)
{
    while ((p != NULL) &&
           ((*p == ' ') || (*p == '\t') || (*p == '\r') || (*p == '\n')))
    {
        p++;
    }
    return p;
}

static uint8_t App_JsonLooksObject(const char *json)
{
    const char *p = App_SkipJsonSpace(json);
    const char *end;

    if ((p == NULL) || (*p != '{'))
    {
        return 0U;
    }

    end = p + strlen(p);
    while ((end > p) &&
           ((end[-1] == ' ') || (end[-1] == '\t') || (end[-1] == '\r') || (end[-1] == '\n')))
    {
        end--;
    }

    return (uint8_t)((end > p) && (end[-1] == '}'));
}

static const char *App_JsonFindValue(const char *json, const char *key)
{
    char pattern[24];
    const char *p;

    if ((json == NULL) || (key == NULL))
    {
        return NULL;
    }

    (void)snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    p = strstr(json, pattern);
    if (p == NULL)
    {
        return NULL;
    }

    p += strlen(pattern);
    p = App_SkipJsonSpace(p);
    if ((p == NULL) || (*p != ':'))
    {
        return NULL;
    }

    return App_SkipJsonSpace(p + 1);
}

static uint8_t App_JsonGetString(const char *json, const char *key, char *out, uint8_t out_len)
{
    const char *p = App_JsonFindValue(json, key);
    uint8_t len = 0U;

    if ((p == NULL) || (out == NULL) || (out_len == 0U) || (*p != '\"'))
    {
        return 0U;
    }

    p++;
    while ((*p != '\0') && (*p != '\"'))
    {
        if (len < (uint8_t)(out_len - 1U))
        {
            out[len++] = *p;
        }
        p++;
    }

    if (*p != '\"')
    {
        out[0] = '\0';
        return 0U;
    }

    out[len] = '\0';
    return 1U;
}

static uint8_t App_JsonGetNumber(const char *json, const char *key, int *out)
{
    const char *p = App_JsonFindValue(json, key);
    int sign = 1;
    int value = 0;

    if ((p == NULL) || (out == NULL))
    {
        return 0U;
    }

    if (*p == '\"')
    {
        p++;
    }
    if (*p == '-')
    {
        sign = -1;
        p++;
    }
    if ((*p < '0') || (*p > '9'))
    {
        return 0U;
    }

    while ((*p >= '0') && (*p <= '9'))
    {
        value = (value * 10) + (*p - '0');
        p++;
    }

    *out = value * sign;
    return 1U;
}

static uint16_t App_GetOccupiedMask(void)
{
    uint8_t i;
    uint16_t mask = 0U;

    for (i = 0U; i < LOCKER_COUNT; i++)
    {
        if (s_store.slots[i].occupied != 0U)
        {
            mask = (uint16_t)(mask | (uint16_t)(1U << i));
        }
    }

    return mask;
}

static uint8_t App_GetProtocolModeValue(void)
{
    return (s_tcpTransport == TCP_TRANSPORT_BINARY) ? 1U : 2U;
}

static uint8_t App_IsDigit(char ch)
{
    return (uint8_t)((ch >= '0') && (ch <= '9'));
}

static uint8_t App_ParseTwoDigits(const char *text, uint8_t *value)
{
    if ((text == NULL) || (value == NULL) || !App_IsDigit(text[0]) || !App_IsDigit(text[1]))
    {
        return 0U;
    }

    *value = (uint8_t)(((uint8_t)(text[0] - '0') * 10U) + (uint8_t)(text[1] - '0'));
    return 1U;
}

static uint16_t App_PackStatusPayload(uint8_t *buf, uint16_t len)
{
    uint16_t occupied_mask = App_GetOccupiedMask();
    uint16_t relay_mask = s_relayMask;
    uint16_t door_mask = s_relayMask;
    uint32_t uptime = HAL_GetTick();
    uint8_t protocol_mode = App_GetProtocolModeValue();

    if ((buf == NULL) || (len < 18U))
    {
        return 0U;
    }

    buf[0] = (uint8_t)(occupied_mask >> 8U);
    buf[1] = (uint8_t)(occupied_mask & 0xFFU);
    buf[2] = (uint8_t)(relay_mask >> 8U);
    buf[3] = (uint8_t)(relay_mask & 0xFFU);
    buf[4] = (uint8_t)(door_mask >> 8U);
    buf[5] = (uint8_t)(door_mask & 0xFFU);
    buf[6] = s_store.alarm;
    buf[7] = s_selectedLocker;
    buf[8] = (uint8_t)(s_store.record_count >> 8U);
    buf[9] = (uint8_t)(s_store.record_count & 0xFFU);
    buf[10] = (uint8_t)(uptime >> 24U);
    buf[11] = (uint8_t)(uptime >> 16U);
    buf[12] = (uint8_t)(uptime >> 8U);
    buf[13] = (uint8_t)(uptime & 0xFFU);
    buf[14] = ESP_IsWifiConnected();
    buf[15] = ESP_IsConnected();
    buf[16] = protocol_mode;
    buf[17] = 0U;

    return 18U;
}

static uint16_t App_PackRecordPayload(const LockerRecord_t *rec, uint8_t *buf, uint16_t len)
{
    uint8_t year = 0U;
    uint8_t month = 0U;
    uint8_t day = 0U;
    uint8_t hour = 0U;
    uint8_t minute = 0U;
    uint8_t second = 0U;
    size_t text_len;

    if ((rec == NULL) || (buf == NULL) || (len < 11U))
    {
        return 0U;
    }

    text_len = strlen(rec->timestamp);
    if ((text_len >= 19U) &&
        (rec->timestamp[0] == '2') &&
        (rec->timestamp[1] == '0') &&
        (App_ParseTwoDigits(&rec->timestamp[2], &year) != 0U) &&
        (App_ParseTwoDigits(&rec->timestamp[5], &month) != 0U) &&
        (App_ParseTwoDigits(&rec->timestamp[8], &day) != 0U) &&
        (App_ParseTwoDigits(&rec->timestamp[11], &hour) != 0U) &&
        (App_ParseTwoDigits(&rec->timestamp[14], &minute) != 0U) &&
        (App_ParseTwoDigits(&rec->timestamp[17], &second) != 0U))
    {
        /* Parsed successfully. */
    }

    buf[0] = rec->type;
    buf[1] = rec->locker_id;
    buf[2] = (uint8_t)(((uint16_t)rec->face_id >> 8U) & 0xFFU);
    buf[3] = (uint8_t)((uint16_t)rec->face_id & 0xFFU);
    buf[4] = rec->result;
    buf[5] = year;
    buf[6] = month;
    buf[7] = day;
    buf[8] = hour;
    buf[9] = minute;
    buf[10] = second;

    return 11U;
}

static uint16_t App_BuildBinaryFrame(uint8_t addr, uint8_t cmd, uint16_t seq, const uint8_t *payload, uint16_t payload_len, uint8_t *frame, uint16_t frame_len)
{
    uint8_t *p;
    uint16_t crc;
    uint16_t required;

    if ((frame == NULL) || (payload_len > TCP_BINARY_MAX_PAYLOAD))
    {
        return 0U;
    }

    required = (uint16_t)(2U + 7U + payload_len + 2U + 2U);
    if (frame_len < required)
    {
        return 0U;
    }

    p = frame;
    *p++ = TCP_BINARY_SOF_0;
    *p++ = TCP_BINARY_SOF_1;
    *p++ = TCP_BINARY_VERSION;
    *p++ = addr;
    *p++ = cmd;
    *p++ = (uint8_t)(seq >> 8U);
    *p++ = (uint8_t)(seq & 0xFFU);
    *p++ = (uint8_t)(payload_len >> 8U);
    *p++ = (uint8_t)(payload_len & 0xFFU);

    if ((payload != NULL) && (payload_len > 0U))
    {
        memcpy(p, payload, payload_len);
        p += payload_len;
    }

    crc = App_CalcCrc16(&frame[2], (uint16_t)(7U + payload_len));
    *p++ = (uint8_t)(crc >> 8U);
    *p++ = (uint8_t)(crc & 0xFFU);
    *p++ = TCP_BINARY_EOF_0;
    *p++ = TCP_BINARY_EOF_1;

    return (uint16_t)(p - frame);
}

static uint8_t App_SendBinaryFrameRaw(const uint8_t *frame, uint16_t len)
{
    if ((frame == NULL) || (len == 0U) || (ESP_IsConnected() == 0U))
    {
        return 0U;
    }

    return (ESP_SendData(0U, frame, len) == ESP_OK) ? 1U : 0U;
}

static void App_ResetBinaryResponseCache(void)
{
    memset(s_tcpBinaryResponseCache, 0, sizeof(s_tcpBinaryResponseCache));
    s_tcpBinaryResponseCacheNext = 0U;
}

static void App_ResetTcpBinaryParserState(void)
{
    s_tcpParseState = TCP_PARSE_TEXT;
    s_tcpLineLen = 0U;
    s_tcpDropLine = 0U;
    s_tcpBinaryHeaderLen = 0U;
    s_tcpBinaryPayloadLen = 0U;
    s_tcpBinaryPayloadIndex = 0U;
    s_tcpBinaryCrcLen = 0U;
    s_tcpBinaryTailLen = 0U;
    s_tcpBinaryAddr = 0U;
    s_tcpBinaryCmd = 0U;
    s_tcpBinarySeq = 0U;
    memset(s_tcpBinaryHeader, 0, sizeof(s_tcpBinaryHeader));
    memset(s_tcpBinaryPayload, 0, sizeof(s_tcpBinaryPayload));
    memset(s_tcpBinaryCrc, 0, sizeof(s_tcpBinaryCrc));
    memset(s_tcpBinaryTail, 0, sizeof(s_tcpBinaryTail));
}

static void App_ResetTcpModeState(void)
{
    s_tcpHelloSent = 0U;
    s_tcpTransport = TCP_TRANSPORT_NONE;
    App_ResetTcpBinaryParserState();
    App_ResetBinaryResponseCache();
}

static uint8_t App_BinaryResponseCacheLookup(uint16_t seq, TcpCommandType_t request_type, TcpBinaryResponseCacheEntry_t *entry)
{
    uint8_t i;

    for (i = 0U; i < TCP_BINARY_RESPONSE_CACHE_SIZE; i++)
    {
        if ((s_tcpBinaryResponseCache[i].valid != 0U) &&
            (s_tcpBinaryResponseCache[i].seq == seq) &&
            (s_tcpBinaryResponseCache[i].request_type == request_type))
        {
            if (entry != NULL)
            {
                *entry = s_tcpBinaryResponseCache[i];
            }
            return 1U;
        }
    }

    return 0U;
}

static void App_BinaryResponseCacheStore(uint16_t seq, TcpCommandType_t request_type, uint8_t response_cmd, const uint8_t *frame, uint16_t frame_len)
{
    TcpBinaryResponseCacheEntry_t *entry;

    if ((frame == NULL) || (frame_len == 0U))
    {
        return;
    }

    entry = &s_tcpBinaryResponseCache[s_tcpBinaryResponseCacheNext];
    s_tcpBinaryResponseCacheNext = (uint8_t)((s_tcpBinaryResponseCacheNext + 1U) % TCP_BINARY_RESPONSE_CACHE_SIZE);
    entry->valid = 1U;
    entry->seq = seq;
    entry->request_type = request_type;
    entry->response_cmd = response_cmd;
    if (frame_len > sizeof(entry->frame))
    {
        frame_len = sizeof(entry->frame);
    }
    entry->frame_len = frame_len;
    memcpy(entry->frame, frame, frame_len);
}

static uint8_t App_SendBinaryResponse(const TcpCommand_t *cmd, uint8_t response_cmd, const uint8_t *payload, uint16_t payload_len, uint8_t cacheable)
{
    uint8_t frame[280U];
    uint16_t frame_len;

    if ((cmd == NULL) || (cmd->has_seq == 0U))
    {
        return 0U;
    }

    frame_len = App_BuildBinaryFrame(TCP_BINARY_DEVICE_ADDR_DEFAULT, response_cmd, cmd->seq, payload, payload_len, frame, sizeof(frame));
    if (frame_len == 0U)
    {
        return 0U;
    }

    if (App_SendBinaryFrameRaw(frame, frame_len) == 0U)
    {
        return 0U;
    }

    if (cacheable != 0U)
    {
        App_BinaryResponseCacheStore(cmd->seq, cmd->type, response_cmd, frame, frame_len);
    }

    return 1U;
}

static void App_SetTcpResponseContext(const TcpCommand_t *cmd)
{
    if ((cmd != NULL) && (cmd->has_seq != 0U))
    {
        s_tcpResponseSeq = cmd->seq;
        s_tcpResponseHasSeq = 1U;
        s_tcpResponseCmd = cmd->type;
    }
    else
    {
        s_tcpResponseSeq = 0U;
        s_tcpResponseHasSeq = 0U;
        s_tcpResponseCmd = TCP_CMD_GET_STATUS;
    }
}

static void App_ClearTcpResponseContext(void)
{
    s_tcpResponseSeq = 0U;
    s_tcpResponseHasSeq = 0U;
    s_tcpResponseCmd = TCP_CMD_GET_STATUS;
}

static void App_ResetTcpProtocolState(void)
{
    s_tcpCmdHead = 0U;
    s_tcpCmdTail = 0U;
    s_tcpCmdCount = 0U;
    s_tcpQueueOverflow = 0U;
    s_exportActive = 0U;
    s_exportIndex = 0U;
    s_exportSeq = 0U;
    s_exportSeqValid = 0U;
    App_ResetTcpModeState();
}

static uint8_t App_EnqueueTcpCommand(const TcpCommand_t *cmd)
{
    if (cmd == NULL)
    {
        return 0U;
    }

    if (s_tcpCmdCount >= TCP_CMD_QUEUE_SIZE)
    {
        s_tcpQueueOverflow = 1U;
        return 0U;
    }

    s_tcpCmdQueue[s_tcpCmdTail] = *cmd;
    s_tcpCmdTail = (uint8_t)((s_tcpCmdTail + 1U) % TCP_CMD_QUEUE_SIZE);
    s_tcpCmdCount++;
    return 1U;
}

static uint8_t App_DequeueTcpCommand(TcpCommand_t *cmd)
{
    if ((cmd == NULL) || (s_tcpCmdCount == 0U))
    {
        return 0U;
    }

    *cmd = s_tcpCmdQueue[s_tcpCmdHead];
    s_tcpCmdHead = (uint8_t)((s_tcpCmdHead + 1U) % TCP_CMD_QUEUE_SIZE);
    s_tcpCmdCount--;
    return 1U;
}

static void App_QueueTcpError(TcpError_t error, uint16_t seq, uint8_t has_seq)
{
    TcpCommand_t cmd;

    memset(&cmd, 0, sizeof(cmd));
    cmd.type = TCP_CMD_ERROR;
    cmd.error = error;
    cmd.seq = seq;
    cmd.has_seq = has_seq;
    (void)App_EnqueueTcpCommand(&cmd);
}

static void App_HandleTcpLine(const char *line)
{
    TcpCommand_t cmd;
    char type[12];
    char name[24];
    int value;
    uint16_t mask;

    memset(&cmd, 0, sizeof(cmd));
    s_tcpTransport = TCP_TRANSPORT_JSON;

    if (App_JsonLooksObject(line) == 0U)
    {
        App_QueueTcpError(TCP_ERR_BAD_JSON, 0U, 0U);
        return;
    }

    if ((App_JsonGetNumber(line, "seq", &value) != 0U) &&
        (value >= 0) && (value <= 65535))
    {
        cmd.seq = (uint16_t)value;
        cmd.has_seq = 1U;
    }

    if ((App_JsonGetString(line, "type", type, sizeof(type)) == 0U) ||
        (strcmp(type, "cmd") != 0))
    {
        App_QueueTcpError(TCP_ERR_INVALID_TYPE, cmd.seq, cmd.has_seq);
        return;
    }

    if (App_JsonGetString(line, "cmd", name, sizeof(name)) == 0U)
    {
        App_QueueTcpError(TCP_ERR_UNKNOWN_CMD, cmd.seq, cmd.has_seq);
        return;
    }

    if ((strcmp(name, "heartbeat") == 0) || (strcmp(name, "ping") == 0))
    {
        cmd.type = TCP_CMD_HEARTBEAT;
    }
    else if ((strcmp(name, "status_query") == 0) || (strcmp(name, "get_status") == 0) || (strcmp(name, "status") == 0))
    {
        cmd.type = TCP_CMD_GET_STATUS;
    }
    else if (strcmp(name, "clear_alarm") == 0)
    {
        cmd.type = TCP_CMD_CLEAR_ALARM;
    }
    else if (strcmp(name, "emergency_unlock") == 0)
    {
        cmd.type = TCP_CMD_EMERGENCY_UNLOCK;
    }
    else if ((strcmp(name, "record_export") == 0) || (strcmp(name, "export_records") == 0))
    {
        cmd.type = TCP_CMD_EXPORT_RECORDS;
    }
    else if (strcmp(name, "set_time") == 0)
    {
        cmd.type = TCP_CMD_SET_TIME;

        if ((App_JsonGetNumber(line, "year", &value) == 0U) || (value < 0) || (value > 99))
        {
            App_QueueTcpError(TCP_ERR_INVALID_TYPE, cmd.seq, cmd.has_seq);
            return;
        }
        cmd.year = (uint8_t)value;

        if ((App_JsonGetNumber(line, "month", &value) == 0U) || (value < 1) || (value > 12))
        {
            App_QueueTcpError(TCP_ERR_INVALID_TYPE, cmd.seq, cmd.has_seq);
            return;
        }
        cmd.month = (uint8_t)value;

        if ((App_JsonGetNumber(line, "day", &value) == 0U) || (value < 1) || (value > 31))
        {
            App_QueueTcpError(TCP_ERR_INVALID_TYPE, cmd.seq, cmd.has_seq);
            return;
        }
        cmd.day = (uint8_t)value;

        if ((App_JsonGetNumber(line, "hour", &value) == 0U) || (value < 0) || (value > 23))
        {
            App_QueueTcpError(TCP_ERR_INVALID_TYPE, cmd.seq, cmd.has_seq);
            return;
        }
        cmd.hour = (uint8_t)value;

        if ((App_JsonGetNumber(line, "minute", &value) == 0U) || (value < 0) || (value > 59))
        {
            App_QueueTcpError(TCP_ERR_INVALID_TYPE, cmd.seq, cmd.has_seq);
            return;
        }
        cmd.minute = (uint8_t)value;

        if ((App_JsonGetNumber(line, "second", &value) == 0U) || (value < 0) || (value > 59))
        {
            App_QueueTcpError(TCP_ERR_INVALID_TYPE, cmd.seq, cmd.has_seq);
            return;
        }
        cmd.second = (uint8_t)value;
        cmd.has_time = 1U;
    }
    else if ((strcmp(name, "open_locker") == 0) || (strcmp(name, "open") == 0))
    {
        if ((App_JsonGetNumber(line, "locker", &value) == 0U) &&
            (App_JsonGetNumber(line, "slot", &value) == 0U))
        {
            App_QueueTcpError(TCP_ERR_INVALID_LOCKER, cmd.seq, cmd.has_seq);
            return;
        }

        if ((value <= 0) || (value > (int)LOCKER_COUNT))
        {
            App_QueueTcpError(TCP_ERR_INVALID_LOCKER, cmd.seq, cmd.has_seq);
            return;
        }

        cmd.type = TCP_CMD_OPEN_LOCKER;
        cmd.locker_mask = (uint16_t)(1U << (uint8_t)(value - 1));
        cmd.action = 0U;
    }
    else if (strcmp(name, "locker_control") == 0)
    {
        if (App_JsonGetNumber(line, "locker_mask", &value) != 0U)
        {
            if ((value <= 0) || (value > 0xFFFF))
            {
                App_QueueTcpError(TCP_ERR_INVALID_LOCKER, cmd.seq, cmd.has_seq);
                return;
            }
            mask = (uint16_t)value;
        }
        else if ((App_JsonGetNumber(line, "locker", &value) != 0U) ||
                 (App_JsonGetNumber(line, "slot", &value) != 0U))
        {
            if ((value <= 0) || (value > (int)LOCKER_COUNT))
            {
                App_QueueTcpError(TCP_ERR_INVALID_LOCKER, cmd.seq, cmd.has_seq);
                return;
            }

            mask = (uint16_t)(1U << (uint8_t)(value - 1));
        }
        else
        {
            App_QueueTcpError(TCP_ERR_INVALID_LOCKER, cmd.seq, cmd.has_seq);
            return;
        }

        if (mask == 0U)
        {
            App_QueueTcpError(TCP_ERR_INVALID_LOCKER, cmd.seq, cmd.has_seq);
            return;
        }

        if ((App_JsonGetNumber(line, "action", &value) == 0U) || (value < 0) || (value > 255))
        {
            App_QueueTcpError(TCP_ERR_INVALID_ACTION, cmd.seq, cmd.has_seq);
            return;
        }

        if ((value < (int)LOCKER_ACTION_OPEN) || (value > (int)LOCKER_ACTION_FORCE_OPEN))
        {
            App_QueueTcpError(TCP_ERR_INVALID_ACTION, cmd.seq, cmd.has_seq);
            return;
        }

        cmd.type = TCP_CMD_LOCKER_CONTROL;
        cmd.locker_mask = mask;
        cmd.action = (uint8_t)value;
        if (App_JsonGetNumber(line, "flags", &value) != 0U)
        {
            cmd.flags = (uint8_t)value;
        }
    }
    else
    {
        App_QueueTcpError(TCP_ERR_UNKNOWN_CMD, cmd.seq, cmd.has_seq);
        return;
    }

    if (s_tcpHelloSent == 0U)
    {
        App_SendHello();
        s_tcpHelloSent = 1U;
        s_lastStatusTick = HAL_GetTick();
    }

    (void)App_EnqueueTcpCommand(&cmd);
}

static uint8_t App_BinaryRequestCodeToType(uint8_t wire_cmd, TcpCommandType_t *type)
{
    if ((type == NULL) || ((wire_cmd & TCP_BINARY_RESPONSE_MASK) != 0U))
    {
        return 0U;
    }

    switch (wire_cmd)
    {
    case TCP_BINARY_CMD_HEARTBEAT:
        *type = TCP_CMD_HEARTBEAT;
        return 1U;
    case TCP_BINARY_CMD_STATUS_QUERY:
        *type = TCP_CMD_GET_STATUS;
        return 1U;
    case TCP_BINARY_CMD_LOCKER_CONTROL:
        *type = TCP_CMD_LOCKER_CONTROL;
        return 1U;
    case TCP_BINARY_CMD_CLEAR_ALARM:
        *type = TCP_CMD_CLEAR_ALARM;
        return 1U;
    case TCP_BINARY_CMD_EMERGENCY_UNLOCK:
        *type = TCP_CMD_EMERGENCY_UNLOCK;
        return 1U;
    case TCP_BINARY_CMD_RECORD_EXPORT:
        *type = TCP_CMD_EXPORT_RECORDS;
        return 1U;
    case TCP_BINARY_CMD_SET_TIME:
        *type = TCP_CMD_SET_TIME;
        return 1U;
    default:
        break;
    }

    return 0U;
}

static uint8_t App_BinaryResponseCodeFromType(TcpCommandType_t type)
{
    uint8_t wire_cmd = 0U;

    switch (type)
    {
    case TCP_CMD_HEARTBEAT:
        wire_cmd = TCP_BINARY_CMD_HEARTBEAT;
        break;
    case TCP_CMD_GET_STATUS:
        wire_cmd = TCP_BINARY_CMD_STATUS_QUERY;
        break;
    case TCP_CMD_OPEN_LOCKER:
    case TCP_CMD_LOCKER_CONTROL:
        wire_cmd = TCP_BINARY_CMD_LOCKER_CONTROL;
        break;
    case TCP_CMD_CLEAR_ALARM:
        wire_cmd = TCP_BINARY_CMD_CLEAR_ALARM;
        break;
    case TCP_CMD_EMERGENCY_UNLOCK:
        wire_cmd = TCP_BINARY_CMD_EMERGENCY_UNLOCK;
        break;
    case TCP_CMD_EXPORT_RECORDS:
        wire_cmd = TCP_BINARY_CMD_RECORD_EXPORT;
        break;
    case TCP_CMD_SET_TIME:
        wire_cmd = TCP_BINARY_CMD_SET_TIME;
        break;
    default:
        break;
    }

    return (uint8_t)(wire_cmd | TCP_BINARY_RESPONSE_MASK);
}

static uint8_t App_GetFirstLockerFromMask(uint16_t mask)
{
    uint8_t i;

    for (i = 0U; i < LOCKER_COUNT; i++)
    {
        if ((mask & (uint16_t)(1U << i)) != 0U)
        {
            return (uint8_t)(i + 1U);
        }
    }

    return 0U;
}

static void App_SetRelayMask(uint16_t mask)
{
    uint8_t i;

    for (i = 0U; i < LOCKER_COUNT; i++)
    {
        Relay_SetState((uint8_t)(i + 1U), ((mask & (uint16_t)(1U << i)) != 0U) ? 1U : 0U);
    }

    s_relayMask = mask;
    s_doorOpen = (mask != 0U) ? 1U : 0U;
}

static uint8_t App_ApplyBinaryLockerControl(uint16_t mask, uint8_t action, uint16_t *applied_mask)
{
    uint16_t current;
    uint16_t next;
    uint16_t applied;
    uint8_t i;

    if (mask == 0U)
    {
        return TCP_BIN_RESULT_INVALID_LOCKER;
    }

    if ((action < LOCKER_ACTION_OPEN) || (action > LOCKER_ACTION_FORCE_OPEN))
    {
        return TCP_BIN_RESULT_INVALID_ACTION;
    }

    current = s_relayMask;
    next = current;
    applied = 0U;

    for (i = 0U; i < LOCKER_COUNT; i++)
    {
        uint16_t bit = (uint16_t)(1U << i);
        uint8_t current_on = ((current & bit) != 0U) ? 1U : 0U;
        uint8_t desired_on = current_on;

        if ((mask & bit) == 0U)
        {
            continue;
        }

        switch (action)
        {
        case LOCKER_ACTION_OPEN:
        case LOCKER_ACTION_FORCE_OPEN:
            desired_on = 1U;
            break;
        case LOCKER_ACTION_CLOSE:
            desired_on = 0U;
            break;
        case LOCKER_ACTION_TOGGLE:
            desired_on = (uint8_t)(current_on == 0U ? 1U : 0U);
            break;
        default:
            return TCP_BIN_RESULT_INVALID_ACTION;
        }

        if (desired_on != current_on)
        {
            applied = (uint16_t)(applied | bit);
        }

        if (desired_on != 0U)
        {
            next = (uint16_t)(next | bit);
        }
        else
        {
            next = (uint16_t)(next & (uint16_t)~bit);
        }
    }

    App_SetRelayMask(next);
    if (applied_mask != NULL)
    {
        *applied_mask = applied;
    }

    return TCP_BIN_RESULT_OK;
}

static uint8_t App_ApplyBinaryClearAlarm(void)
{
    s_store.alarm = 0U;
    Buzzer_Off();
    App_SaveStorage();
    return TCP_BIN_RESULT_OK;
}

static uint8_t App_ApplyBinaryEmergencyUnlock(uint16_t *applied_mask)
{
    uint16_t next = 0xFFFFU;
    uint16_t applied = (uint16_t)(next ^ s_relayMask);

    App_SetRelayMask(next);
    s_store.alarm = 0U;
    Buzzer_Off();
    App_SaveStorage();

    if (applied_mask != NULL)
    {
        *applied_mask = applied;
    }

    return TCP_BIN_RESULT_OK;
}

static uint8_t App_ApplyBinarySetTime(const TcpCommand_t *cmd)
{
    PCF8563_Time_t time;
    HAL_StatusTypeDef st;

    if ((cmd == NULL) || (cmd->has_time == 0U))
    {
        return TCP_BIN_RESULT_BAD_REQUEST;
    }

    memset(&time, 0, sizeof(time));
    time.year = cmd->year;
    time.month = cmd->month;
    time.day = cmd->day;
    time.weekday = App_CalcWeekday((uint16_t)(2000U + cmd->year), cmd->month, cmd->day);
    time.hour = cmd->hour;
    time.minute = cmd->minute;
    time.second = cmd->second;
    time.valid = 1U;

    st = PCF8563_SetTime(&time);
    return (st == HAL_OK) ? TCP_BIN_RESULT_OK : TCP_BIN_RESULT_BUSY;
}

static uint8_t App_SendBinaryStatusResult(TcpCommandType_t request_type, uint16_t seq, uint8_t result, uint8_t cacheable)
{
    TcpCommand_t resp;
    uint8_t payload[19U];

    memset(&resp, 0, sizeof(resp));
    resp.type = request_type;
    resp.seq = seq;
    resp.has_seq = 1U;
    payload[0] = result;
    if (App_PackStatusPayload(&payload[1], (uint16_t)(sizeof(payload) - 1U)) == 0U)
    {
        return 0U;
    }

    return App_SendBinaryResponse(&resp, App_BinaryResponseCodeFromType(request_type), payload, sizeof(payload), cacheable);
}

static uint8_t App_SendBinaryControlResult(TcpCommandType_t request_type, uint16_t seq, uint8_t result, uint16_t applied_mask, uint8_t cacheable)
{
    TcpCommand_t resp;
    uint8_t payload[21U];

    memset(&resp, 0, sizeof(resp));
    resp.type = request_type;
    resp.seq = seq;
    resp.has_seq = 1U;
    payload[0] = result;
    payload[1] = (uint8_t)(applied_mask >> 8U);
    payload[2] = (uint8_t)(applied_mask & 0xFFU);
    if (App_PackStatusPayload(&payload[3], (uint16_t)(sizeof(payload) - 3U)) == 0U)
    {
        return 0U;
    }

    return App_SendBinaryResponse(&resp, App_BinaryResponseCodeFromType(request_type), payload, sizeof(payload), cacheable);
}

static uint8_t App_SendBinaryExportBegin(uint16_t seq, uint8_t result)
{
    TcpCommand_t resp;
    uint8_t payload[4U];

    memset(&resp, 0, sizeof(resp));
    resp.type = TCP_CMD_EXPORT_RECORDS;
    resp.seq = seq;
    resp.has_seq = 1U;
    payload[0] = 0U;
    payload[1] = result;
    payload[2] = (uint8_t)(s_store.record_count >> 8U);
    payload[3] = (uint8_t)(s_store.record_count & 0xFFU);

    return App_SendBinaryResponse(&resp, App_BinaryResponseCodeFromType(TCP_CMD_EXPORT_RECORDS), payload, sizeof(payload), 0U);
}

static uint8_t App_SendBinaryExportRecord(const LockerRecord_t *rec)
{
    TcpCommand_t resp;
    uint8_t payload[13U];
    uint16_t record_len;

    if (rec == NULL)
    {
        return 0U;
    }

    memset(&resp, 0, sizeof(resp));
    resp.type = TCP_CMD_EXPORT_RECORDS;
    resp.seq = s_exportSeq;
    resp.has_seq = 1U;
    payload[0] = 1U;
    payload[1] = TCP_BIN_RESULT_OK;
    record_len = App_PackRecordPayload(rec, &payload[2], (uint16_t)(sizeof(payload) - 2U));
    if (record_len == 0U)
    {
        return 0U;
    }

    return App_SendBinaryResponse(&resp, App_BinaryResponseCodeFromType(TCP_CMD_EXPORT_RECORDS), payload, (uint16_t)(record_len + 2U), 0U);
}

static uint8_t App_SendBinaryExportDone(uint16_t seq, uint8_t result)
{
    TcpCommand_t resp;
    uint8_t payload[4U];

    memset(&resp, 0, sizeof(resp));
    resp.type = TCP_CMD_EXPORT_RECORDS;
    resp.seq = seq;
    resp.has_seq = 1U;
    payload[0] = 2U;
    payload[1] = result;
    payload[2] = (uint8_t)(s_store.record_count >> 8U);
    payload[3] = (uint8_t)(s_store.record_count & 0xFFU);

    return App_SendBinaryResponse(&resp, App_BinaryResponseCodeFromType(TCP_CMD_EXPORT_RECORDS), payload, sizeof(payload), 0U);
}

static void App_ProcessBinaryFrame(void)
{
    TcpCommandType_t type;
    TcpBinaryResponseCacheEntry_t cached;
    TcpCommand_t cmd;
    uint16_t applied_mask = 0U;
    uint8_t result = TCP_BIN_RESULT_OK;
    uint16_t locker_mask = 0U;
    uint8_t action = 0U;
    uint8_t flags = 0U;

    if ((s_tcpBinaryAddr != TCP_BINARY_DEVICE_ADDR_DEFAULT) && (s_tcpBinaryAddr != 0xFFU))
    {
        return;
    }

    if ((s_tcpBinaryCmd & TCP_BINARY_RESPONSE_MASK) != 0U)
    {
        return;
    }

    if (App_BinaryRequestCodeToType((uint8_t)(s_tcpBinaryCmd & 0x7FU), &type) == 0U)
    {
        return;
    }

    s_tcpTransport = TCP_TRANSPORT_BINARY;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = type;
    cmd.seq = s_tcpBinarySeq;
    cmd.has_seq = 1U;
    cmd.wire_cmd = (uint8_t)(s_tcpBinaryCmd & 0x7FU);

    if (App_BinaryResponseCacheLookup(cmd.seq, type, &cached) != 0U)
    {
        (void)App_SendBinaryFrameRaw(cached.frame, cached.frame_len);
        return;
    }

    switch (type)
    {
    case TCP_CMD_HEARTBEAT:
    case TCP_CMD_GET_STATUS:
        (void)App_SendBinaryStatusResult(type, cmd.seq, TCP_BIN_RESULT_OK, 1U);
        break;

    case TCP_CMD_CLEAR_ALARM:
        result = App_ApplyBinaryClearAlarm();
        (void)App_SendBinaryStatusResult(type, cmd.seq, result, 1U);
        break;

    case TCP_CMD_LOCKER_CONTROL:
        if (s_tcpBinaryPayloadLen < 4U)
        {
            (void)App_SendBinaryControlResult(type, cmd.seq, TCP_BIN_RESULT_BAD_REQUEST, 0U, 1U);
            break;
        }

        locker_mask = (uint16_t)(((uint16_t)s_tcpBinaryPayload[0] << 8U) | s_tcpBinaryPayload[1]);
        action = s_tcpBinaryPayload[2];
        flags = s_tcpBinaryPayload[3];
        cmd.locker_mask = locker_mask;
        cmd.action = action;
        cmd.flags = flags;
        result = App_ApplyBinaryLockerControl(locker_mask, action, &applied_mask);
        if ((result == TCP_BIN_RESULT_OK) && (applied_mask != 0U))
        {
            uint8_t locker_id = App_GetFirstLockerFromMask(applied_mask);
            if (locker_id != 0U)
            {
                App_AddRecord(6U, locker_id, -1, 1U);
            }
        }
        (void)App_SendBinaryControlResult(type, cmd.seq, result, applied_mask, 1U);
        break;

    case TCP_CMD_EMERGENCY_UNLOCK:
        result = App_ApplyBinaryEmergencyUnlock(&applied_mask);
        App_AddRecord(7U, 0U, -1, (uint8_t)(result == TCP_BIN_RESULT_OK));
        (void)App_SendBinaryControlResult(type, cmd.seq, result, applied_mask, 1U);
        break;

    case TCP_CMD_SET_TIME:
        if (s_tcpBinaryPayloadLen < 6U)
        {
            (void)App_SendBinaryStatusResult(type, cmd.seq, TCP_BIN_RESULT_BAD_REQUEST, 1U);
            break;
        }

        cmd.year = s_tcpBinaryPayload[0];
        cmd.month = s_tcpBinaryPayload[1];
        cmd.day = s_tcpBinaryPayload[2];
        cmd.hour = s_tcpBinaryPayload[3];
        cmd.minute = s_tcpBinaryPayload[4];
        cmd.second = s_tcpBinaryPayload[5];
        cmd.has_time = 1U;
        result = App_ApplyBinarySetTime(&cmd);
        if (result == TCP_BIN_RESULT_OK)
        {
            App_AddRecord(8U, 0U, -1, 1U);
        }
        (void)App_SendBinaryStatusResult(type, cmd.seq, result, 1U);
        break;

    case TCP_CMD_EXPORT_RECORDS:
        if (s_exportActive != 0U)
        {
            if ((s_exportSeqValid != 0U) && (s_exportSeq == s_tcpBinarySeq))
            {
                return;
            }

            (void)App_SendBinaryExportDone(s_tcpBinarySeq, TCP_BIN_RESULT_BUSY);
        }
        else
        {
            App_StartRecordExport(&cmd, 0U);
        }
        break;

    default:
        break;
    }
}

static void App_OnTcpPacket(ESP_DataPacket_t *packet)
{
    uint16_t i;

    if (packet == NULL)
    {
        return;
    }

    for (i = 0U; i < packet->len; i++)
    {
        uint8_t ch = packet->data[i];

        switch (s_tcpParseState)
        {
        case TCP_PARSE_TEXT:
            if (ch == '\r')
            {
                continue;
            }

            if (ch == TCP_BINARY_SOF_0)
            {
                s_tcpLineLen = 0U;
                s_tcpDropLine = 0U;
                s_tcpBinaryHeaderLen = 0U;
                s_tcpBinaryPayloadLen = 0U;
                s_tcpBinaryPayloadIndex = 0U;
                s_tcpBinaryCrcLen = 0U;
                s_tcpBinaryTailLen = 0U;
                s_tcpParseState = TCP_PARSE_SOF_2;
                s_tcpBinaryHeader[0] = ch;
                continue;
            }

            if (ch == '\n')
            {
                if (s_tcpDropLine != 0U)
                {
                    s_tcpDropLine = 0U;
                    s_tcpLineLen = 0U;
                    App_QueueTcpError(TCP_ERR_LINE_TOO_LONG, 0U, 0U);
                }
                else if (s_tcpLineLen > 0U)
                {
                    s_tcpLine[s_tcpLineLen] = '\0';
                    App_HandleTcpLine(s_tcpLine);
                    s_tcpLineLen = 0U;
                }
                continue;
            }

            if (s_tcpDropLine != 0U)
            {
                continue;
            }

            if (s_tcpLineLen < TCP_LINE_MAX)
            {
                s_tcpLine[s_tcpLineLen++] = (char)ch;
            }
            else
            {
                s_tcpLineLen = 0U;
                s_tcpDropLine = 1U;
            }
            break;

        case TCP_PARSE_SOF_2:
            if (ch == TCP_BINARY_SOF_1)
            {
                s_tcpParseState = TCP_PARSE_BINARY_HEADER;
                s_tcpBinaryHeaderLen = 0U;
                s_tcpBinaryPayloadLen = 0U;
                s_tcpBinaryPayloadIndex = 0U;
                s_tcpBinaryCrcLen = 0U;
                s_tcpBinaryTailLen = 0U;
                memset(s_tcpBinaryHeader, 0, sizeof(s_tcpBinaryHeader));
                memset(s_tcpBinaryPayload, 0, sizeof(s_tcpBinaryPayload));
                memset(s_tcpBinaryCrc, 0, sizeof(s_tcpBinaryCrc));
                memset(s_tcpBinaryTail, 0, sizeof(s_tcpBinaryTail));
            }
            else if (ch == TCP_BINARY_SOF_0)
            {
                s_tcpParseState = TCP_PARSE_SOF_2;
            }
            else
            {
                s_tcpParseState = TCP_PARSE_TEXT;
            }
            break;

        case TCP_PARSE_BINARY_HEADER:
            s_tcpBinaryHeader[s_tcpBinaryHeaderLen++] = ch;
            if (s_tcpBinaryHeaderLen < sizeof(s_tcpBinaryHeader))
            {
                break;
            }

            if (s_tcpBinaryHeader[0] != TCP_BINARY_VERSION)
            {
                App_ResetTcpBinaryParserState();
                break;
            }

            s_tcpBinaryAddr = s_tcpBinaryHeader[1];
            s_tcpBinaryCmd = s_tcpBinaryHeader[2];
            s_tcpBinarySeq = (uint16_t)(((uint16_t)s_tcpBinaryHeader[3] << 8U) | s_tcpBinaryHeader[4]);
            s_tcpBinaryPayloadLen = (uint16_t)(((uint16_t)s_tcpBinaryHeader[5] << 8U) | s_tcpBinaryHeader[6]);
            if (s_tcpBinaryPayloadLen > TCP_BINARY_MAX_PAYLOAD)
            {
                App_ResetTcpBinaryParserState();
                break;
            }

            s_tcpBinaryPayloadIndex = 0U;
            s_tcpBinaryCrcLen = 0U;
            s_tcpBinaryTailLen = 0U;
            if (s_tcpBinaryPayloadLen == 0U)
            {
                s_tcpParseState = TCP_PARSE_BINARY_CRC;
            }
            else
            {
                s_tcpParseState = TCP_PARSE_BINARY_PAYLOAD;
            }
            break;

        case TCP_PARSE_BINARY_PAYLOAD:
            s_tcpBinaryPayload[s_tcpBinaryPayloadIndex++] = ch;
            if (s_tcpBinaryPayloadIndex >= s_tcpBinaryPayloadLen)
            {
                s_tcpParseState = TCP_PARSE_BINARY_CRC;
                s_tcpBinaryCrcLen = 0U;
            }
            break;

        case TCP_PARSE_BINARY_CRC:
            s_tcpBinaryCrc[s_tcpBinaryCrcLen++] = ch;
            if (s_tcpBinaryCrcLen >= sizeof(s_tcpBinaryCrc))
            {
                uint16_t expected = App_CalcCrc16(s_tcpBinaryHeader, (uint16_t)(sizeof(s_tcpBinaryHeader) + s_tcpBinaryPayloadLen));
                uint16_t received = (uint16_t)(((uint16_t)s_tcpBinaryCrc[0] << 8U) | s_tcpBinaryCrc[1]);

                if (received != expected)
                {
                    App_ResetTcpBinaryParserState();
                    break;
                }

                s_tcpParseState = TCP_PARSE_BINARY_TAIL;
                s_tcpBinaryTailLen = 0U;
            }
            break;

        case TCP_PARSE_BINARY_TAIL:
            s_tcpBinaryTail[s_tcpBinaryTailLen++] = ch;
            if (s_tcpBinaryTailLen >= sizeof(s_tcpBinaryTail))
            {
                if ((s_tcpBinaryTail[0] == TCP_BINARY_EOF_0) && (s_tcpBinaryTail[1] == TCP_BINARY_EOF_1))
                {
                    App_ProcessBinaryFrame();
                }
                App_ResetTcpBinaryParserState();
            }
            break;

        default:
            App_ResetTcpBinaryParserState();
            break;
        }
    }
}

static void App_SendTcpLine(const char *line)
{
    if ((line != NULL) && (ESP_IsConnected() != 0U) && (s_tcpTransport == TCP_TRANSPORT_JSON))
    {
        (void)ESP_SendString(0U, line);
    }
}

static const char *App_TcpCommandName(TcpCommandType_t type)
{
    switch (type)
    {
    case TCP_CMD_HEARTBEAT:
        return "heartbeat";
    case TCP_CMD_GET_STATUS:
        return "status_query";
    case TCP_CMD_OPEN_LOCKER:
        return "open_locker";
    case TCP_CMD_CLEAR_ALARM:
        return "clear_alarm";
    case TCP_CMD_LOCKER_CONTROL:
        return "locker_control";
    case TCP_CMD_EMERGENCY_UNLOCK:
        return "emergency_unlock";
    case TCP_CMD_EXPORT_RECORDS:
        return "record_export";
    case TCP_CMD_SET_TIME:
        return "set_time";
    case TCP_CMD_ERROR:
        return "protocol_error";
    default:
        return "unknown";
    }
}

static const char *App_TcpErrorCode(TcpError_t error)
{
    switch (error)
    {
    case TCP_ERR_BAD_JSON:
        return "bad_json";
    case TCP_ERR_INVALID_TYPE:
        return "invalid_type";
    case TCP_ERR_UNKNOWN_CMD:
    case TCP_ERR_UNSUPPORTED:
        return "unsupported";
    case TCP_ERR_INVALID_LOCKER:
        return "invalid_locker";
    case TCP_ERR_INVALID_ACTION:
        return "invalid_action";
    case TCP_ERR_LINE_TOO_LONG:
        return "line_too_long";
    case TCP_ERR_QUEUE_FULL:
        return "queue_full";
    case TCP_ERR_CRC_ERROR:
        return "crc_error";
    case TCP_ERR_BUSY:
        return "busy";
    case TCP_ERR_TIMEOUT:
        return "timeout";
    default:
        return "unknown_error";
    }
}

static const char *App_TcpErrorMessage(TcpError_t error)
{
    switch (error)
    {
    case TCP_ERR_BAD_JSON:
        return "message must be one JSON object";
    case TCP_ERR_INVALID_TYPE:
        return "type must be cmd";
    case TCP_ERR_UNKNOWN_CMD:
        return "cmd is not supported";
    case TCP_ERR_INVALID_LOCKER:
        return "locker must be 1 to 16";
    case TCP_ERR_INVALID_ACTION:
        return "action must be 1 to 4";
    case TCP_ERR_LINE_TOO_LONG:
        return "line exceeds 256 bytes";
    case TCP_ERR_QUEUE_FULL:
        return "command queue is full";
    case TCP_ERR_CRC_ERROR:
        return "crc check failed";
    case TCP_ERR_BUSY:
        return "device is busy";
    case TCP_ERR_TIMEOUT:
        return "response timeout";
    case TCP_ERR_UNSUPPORTED:
        return "command is not supported";
    default:
        return "unknown protocol error";
    }
}

static const char *App_RecordTypeName(uint8_t type)
{
    switch (type)
    {
    case 1U:
        return "store";
    case 2U:
        return "take";
    case 3U:
        return "admin_open";
    case 4U:
        return "clear_all";
    case 5U:
        return "door_timeout_alarm";
    case 6U:
        return "remote_control";
    case 7U:
        return "emergency_unlock";
    case 8U:
        return "set_time";
    default:
        return "unknown";
    }
}

static void App_BuildLockerMap(char *buf, uint16_t len)
{
    uint8_t i;
    uint16_t used = 0U;
    int written;

    if ((buf == NULL) || (len == 0U))
    {
        return;
    }

    buf[0] = '\0';
    written = snprintf(buf, len, "[");
    if ((written < 0) || ((uint16_t)written >= len))
    {
        buf[len - 1U] = '\0';
        return;
    }
    used = (uint16_t)written;

    for (i = 0U; i < LOCKER_COUNT; i++)
    {
        written = snprintf(&buf[used], (uint16_t)(len - used), "%u%s",
                           (unsigned int)(s_store.slots[i].occupied != 0U),
                           (i == (LOCKER_COUNT - 1U)) ? "]" : ",");
        if ((written < 0) || ((uint16_t)written >= (uint16_t)(len - used)))
        {
            buf[len - 1U] = '\0';
            return;
        }
        used = (uint16_t)(used + (uint16_t)written);
    }
}

static void App_SendHello(void)
{
    char json[128];

    (void)snprintf(json, sizeof(json),
                   "{\"type\":\"hello\",\"device\":\"locker\",\"proto\":%u,\"protocol_mode\":2,\"transport\":\"legacy_json\",\"ip\":\"%s\",\"port\":%u,\"slots\":%u}\n",
                   (unsigned int)TCP_PROTOCOL_VERSION,
                   ESP_AP_IP,
                   (unsigned int)ESP_TCP_PORT,
                   (unsigned int)LOCKER_COUNT);
    App_SendTcpLine(json);
}

static void App_SendAck(const TcpCommand_t *cmd)
{
    char json[96];

    if (s_tcpTransport != TCP_TRANSPORT_JSON)
    {
        return;
    }

    if ((cmd != NULL) && (cmd->has_seq != 0U))
    {
        (void)snprintf(json, sizeof(json),
                       "{\"type\":\"ack\",\"seq\":%u,\"cmd\":\"%s\",\"result\":\"ok\"}\n",
                       (unsigned int)cmd->seq,
                       App_TcpCommandName(cmd->type));
    }
    else
    {
        (void)snprintf(json, sizeof(json),
                       "{\"type\":\"ack\",\"cmd\":\"%s\",\"result\":\"ok\"}\n",
                       (cmd != NULL) ? App_TcpCommandName(cmd->type) : "unknown");
    }
    App_SendTcpLine(json);
}

static void App_SendErrorLine(const TcpCommand_t *cmd, const char *code, const char *message)
{
    char json[144];

    if (s_tcpTransport != TCP_TRANSPORT_JSON)
    {
        return;
    }

    if ((cmd != NULL) && (cmd->has_seq != 0U))
    {
        (void)snprintf(json, sizeof(json),
                       "{\"type\":\"error\",\"seq\":%u,\"code\":\"%s\",\"message\":\"%s\"}\n",
                       (unsigned int)cmd->seq,
                       code,
                       message);
    }
    else
    {
        (void)snprintf(json, sizeof(json),
                       "{\"type\":\"error\",\"code\":\"%s\",\"message\":\"%s\"}\n",
                       code,
                       message);
    }
    App_SendTcpLine(json);
}

static void App_SendProtocolError(const TcpCommand_t *cmd)
{
    TcpError_t error;

    if (s_tcpTransport != TCP_TRANSPORT_JSON)
    {
        return;
    }

    error = (cmd != NULL) ? cmd->error : TCP_ERR_QUEUE_FULL;
    App_SendErrorLine(cmd, App_TcpErrorCode(error), App_TcpErrorMessage(error));
}

static void App_SendStatus(void)
{
    if (s_tcpTransport == TCP_TRANSPORT_BINARY)
    {
        (void)App_SendBinaryStatusResult(s_tcpResponseCmd, s_tcpResponseSeq, TCP_BIN_RESULT_OK, s_tcpResponseHasSeq);
        return;
    }
    if (s_tcpTransport != TCP_TRANSPORT_JSON)
    {
        return;
    }

    {
        char json[320];
        char lockers[48];
        uint8_t used = App_CountUsed();
        uint16_t occupied_mask = App_GetOccupiedMask();

        App_BuildLockerMap(lockers, sizeof(lockers));
        (void)snprintf(json, sizeof(json),
                       "{\"type\":\"status\",\"proto\":%u,\"protocol_mode\":2,\"transport\":\"legacy_json\",\"slots\":%u,\"used\":%u,\"free\":%u,\"alarm\":%u,\"door_open\":%u,\"locker\":%u,\"selected_locker\":%u,\"occupied_mask\":%u,\"relay_mask\":%u,\"door_mask\":%u,\"record_count\":%u,\"uptime_ms\":%lu,\"wifi_connected\":%u,\"tcp_connected\":%u,\"lockers\":%s}\n",
                       (unsigned int)TCP_PROTOCOL_VERSION,
                       (unsigned int)LOCKER_COUNT,
                       (unsigned int)used,
                       (unsigned int)(LOCKER_COUNT - used),
                       (unsigned int)s_store.alarm,
                       (unsigned int)s_doorOpen,
                       (unsigned int)s_selectedLocker,
                       (unsigned int)s_selectedLocker,
                       (unsigned int)occupied_mask,
                       (unsigned int)s_relayMask,
                       (unsigned int)s_relayMask,
                       (unsigned int)s_store.record_count,
                       (unsigned long)HAL_GetTick(),
                       (unsigned int)ESP_IsWifiConnected(),
                       (unsigned int)ESP_IsConnected(),
                       lockers);
        App_SendTcpLine(json);
    }
}

static void App_SendRecord(const LockerRecord_t *rec)
{
    if (rec == NULL)
    {
        return;
    }

    if (s_tcpTransport == TCP_TRANSPORT_BINARY)
    {
        (void)App_SendBinaryExportRecord(rec);
    }
    else if (s_tcpTransport == TCP_TRANSPORT_JSON)
    {
        char json[224];

        if (s_exportSeqValid != 0U)
        {
            (void)snprintf(json, sizeof(json),
                           "{\"type\":\"record\",\"seq\":%u,\"op\":%u,\"op_name\":\"%s\",\"locker\":%u,\"face\":%d,\"result\":%u,\"time\":\"%s\"}\n",
                           (unsigned int)s_exportSeq,
                           (unsigned int)rec->type,
                           App_RecordTypeName(rec->type),
                           (unsigned int)rec->locker_id,
                           (int)rec->face_id,
                           (unsigned int)rec->result,
                           rec->timestamp);
        }
        else
        {
            (void)snprintf(json, sizeof(json),
                           "{\"type\":\"record\",\"op\":%u,\"op_name\":\"%s\",\"locker\":%u,\"face\":%d,\"result\":%u,\"time\":\"%s\"}\n",
                           (unsigned int)rec->type,
                           App_RecordTypeName(rec->type),
                           (unsigned int)rec->locker_id,
                           (int)rec->face_id,
                           (unsigned int)rec->result,
                           rec->timestamp);
        }
        App_SendTcpLine(json);
    }
}

static void App_SendExportDone(void)
{
    if (s_tcpTransport == TCP_TRANSPORT_BINARY)
    {
        (void)App_SendBinaryExportDone(s_exportSeqValid != 0U ? s_exportSeq : 0U, TCP_BIN_RESULT_OK);
    }
    else if (s_tcpTransport == TCP_TRANSPORT_JSON)
    {
        char json[80];

        if (s_exportSeqValid != 0U)
        {
            (void)snprintf(json, sizeof(json),
                           "{\"type\":\"export_done\",\"seq\":%u,\"count\":%u}\n",
                           (unsigned int)s_exportSeq,
                           (unsigned int)s_store.record_count);
        }
        else
        {
            (void)snprintf(json, sizeof(json),
                           "{\"type\":\"export_done\",\"count\":%u}\n",
                           (unsigned int)s_store.record_count);
        }
        App_SendTcpLine(json);
    }
}

static void App_StartRecordExport(const TcpCommand_t *cmd, uint8_t send_ack)
{
    s_exportActive = 1U;
    s_exportIndex = 0U;
    s_exportSeq = (cmd != NULL) ? cmd->seq : 0U;
    s_exportSeqValid = ((cmd != NULL) && (cmd->has_seq != 0U)) ? 1U : 0U;

    if (s_tcpTransport == TCP_TRANSPORT_BINARY)
    {
        (void)App_SendBinaryExportBegin(s_exportSeq, TCP_BIN_RESULT_OK);
        if (s_store.record_count == 0U)
        {
            (void)App_SendBinaryExportDone(s_exportSeq, TCP_BIN_RESULT_OK);
            s_exportActive = 0U;
            s_exportSeqValid = 0U;
        }
    }
    else if (send_ack != 0U)
    {
        App_SendAck(cmd);
    }
}

static void App_ProcessTcpCommand(const TcpCommand_t *cmd)
{
    uint16_t applied_mask = 0U;
    uint8_t result;

    if (cmd == NULL)
    {
        return;
    }

    switch (cmd->type)
    {
    case TCP_CMD_ERROR:
        App_SendProtocolError(cmd);
        break;

    case TCP_CMD_HEARTBEAT:
        App_SendAck(cmd);
        App_SendStatus();
        s_statusPushPending = 0U;
        s_lastStatusTick = HAL_GetTick();
        break;

    case TCP_CMD_GET_STATUS:
        App_SendAck(cmd);
        App_SendStatus();
        s_statusPushPending = 0U;
        s_lastStatusTick = HAL_GetTick();
        break;

    case TCP_CMD_OPEN_LOCKER:
        if ((cmd->locker_mask == 0U) || (App_GetFirstLockerFromMask(cmd->locker_mask) == 0U))
        {
            App_SendErrorLine(cmd, "invalid_locker", "locker must be 1 to 16");
        }
        else if (s_doorOpen != 0U)
        {
            App_SendErrorLine(cmd, "door_busy", "door is already open");
        }
        else if (App_StartDoorOpen(App_GetFirstLockerFromMask(cmd->locker_mask), OPEN_ADMIN) != 0U)
        {
            App_SendAck(cmd);
            App_SendStatus();
            s_statusPushPending = 0U;
            s_lastStatusTick = HAL_GetTick();
        }
        else
        {
            App_SendErrorLine(cmd, "door_busy", "door is already open");
        }
        break;

    case TCP_CMD_LOCKER_CONTROL:
        result = App_ApplyBinaryLockerControl(cmd->locker_mask, cmd->action, &applied_mask);
        if (result == TCP_BIN_RESULT_OK)
        {
            App_SendAck(cmd);
            App_SendStatus();
            s_statusPushPending = 0U;
            s_lastStatusTick = HAL_GetTick();
            if (applied_mask != 0U)
            {
                uint8_t locker_id = App_GetFirstLockerFromMask(applied_mask);
                if (locker_id != 0U)
                {
                    App_AddRecord(6U, locker_id, -1, 1U);
                }
            }
        }
        else if (result == TCP_BIN_RESULT_INVALID_LOCKER)
        {
            App_SendErrorLine(cmd, "invalid_locker", "locker mask is empty");
        }
        else if (result == TCP_BIN_RESULT_INVALID_ACTION)
        {
            App_SendErrorLine(cmd, "invalid_action", "action must be 1 to 4");
        }
        else
        {
            App_SendErrorLine(cmd, "busy", "relay control failed");
        }
        break;

    case TCP_CMD_CLEAR_ALARM:
        (void)App_ApplyBinaryClearAlarm();
        App_SendAck(cmd);
        App_SendStatus();
        s_statusPushPending = 0U;
        s_lastStatusTick = HAL_GetTick();
        App_RequestStatusRedraw();
        break;

    case TCP_CMD_EMERGENCY_UNLOCK:
        result = App_ApplyBinaryEmergencyUnlock(&applied_mask);
        if (result == TCP_BIN_RESULT_OK)
        {
            App_SendAck(cmd);
            App_SendStatus();
            s_statusPushPending = 0U;
            s_lastStatusTick = HAL_GetTick();
            App_RequestStatusRedraw();
            App_AddRecord(7U, 0U, -1, 1U);
        }
        else
        {
            App_SendErrorLine(cmd, "busy", "emergency unlock failed");
        }
        break;

    case TCP_CMD_EXPORT_RECORDS:
        if (s_exportActive != 0U)
        {
            App_SendErrorLine(cmd, "export_busy", "record export is running");
        }
        else
        {
            App_StartRecordExport(cmd, 1U);
            if (s_store.record_count != 0U)
            {
                App_SendStatus();
                s_lastStatusTick = HAL_GetTick();
            }
        }
        break;

    case TCP_CMD_SET_TIME:
        result = App_ApplyBinarySetTime(cmd);
        if (result == TCP_BIN_RESULT_OK)
        {
            App_SendAck(cmd);
            App_SendStatus();
            s_statusPushPending = 0U;
            s_lastStatusTick = HAL_GetTick();
            App_AddRecord(8U, 0U, -1, 1U);
        }
        else
        {
            App_SendErrorLine(cmd, "time_failed", "rtc set time failed");
        }
        break;

    default:
        App_SendErrorLine(cmd, "unknown_cmd", "cmd is not supported");
        break;
    }
}

static void App_ProcessNetwork(void)
{
    TcpCommand_t cmd;
    uint32_t now;

    ESP_Process();
    now = HAL_GetTick();

    if (ESP_IsConnected() == 0U)
    {
        s_tcpHelloSent = 0U;
        App_ResetTcpProtocolState();
        return;
    }

    if (s_tcpQueueOverflow != 0U)
    {
        TcpCommand_t error_cmd;
        memset(&error_cmd, 0, sizeof(error_cmd));
        error_cmd.type = TCP_CMD_ERROR;
        error_cmd.error = TCP_ERR_QUEUE_FULL;
        App_SendProtocolError(&error_cmd);
        s_tcpQueueOverflow = 0U;
    }

    if (App_DequeueTcpCommand(&cmd) != 0U)
    {
        App_ProcessTcpCommand(&cmd);
    }

    if (s_localExportRequest != 0U)
    {
        s_localExportRequest = 0U;
        if (s_exportActive == 0U)
        {
            App_StartRecordExport(NULL, 0U);
            if (s_store.record_count != 0U)
            {
                App_SendStatus();
                s_lastStatusTick = now;
            }
        }
    }

    if (s_exportActive != 0U)
    {
        if (s_exportIndex < s_store.record_count)
        {
            uint16_t base = (s_store.record_head + LOCKER_RECORD_COUNT - s_store.record_count) % LOCKER_RECORD_COUNT;
            uint16_t idx = (uint16_t)((base + s_exportIndex) % LOCKER_RECORD_COUNT);
            App_SendRecord(&s_store.records[idx]);
            s_exportIndex++;
        }
        else
        {
            App_SendExportDone();
            s_exportActive = 0U;
            s_exportSeqValid = 0U;
            App_SendStatus();
            s_statusPushPending = 0U;
            s_lastStatusTick = HAL_GetTick();
        }
    }

    now = HAL_GetTick();
    if (s_statusPushPending != 0U)
    {
        s_statusPushPending = 0U;
        App_SendStatus();
        s_lastStatusTick = now;
    }
    else if ((now - s_lastStatusTick) >= LOCKER_STATUS_INTERVAL_MS)
    {
        App_SendStatus();
        s_lastStatusTick = now;
    }
}

static void App_OnFaceMatched(int16_t face_id, const char *name)
{
    (void)name;
    s_faceMatchedId = face_id;
    s_faceMatchedFlag = 1U;
}

static void App_OnFaceUnmatched(void)
{
    s_faceUnmatchedFlag = 1U;
}

static void App_OnFaceInvalid(uint8_t error)
{
    s_lastFaceError = error;
    s_faceInvalidFlag = 1U;
}

static void App_OnFaceInfo(int16_t status, int16_t left, int16_t top,
                           int16_t right, int16_t bottom,
                           int16_t yaw, int16_t pitch, int16_t roll)
{
    s_faceStatus = status;
    s_faceLeft = left;
    s_faceTop = top;
    s_faceRight = right;
    s_faceBottom = bottom;
    s_faceYaw = yaw;
    s_facePitch = pitch;
    s_faceRoll = roll;

    if ((s_page == PAGE_STORE_FACE) || (s_page == PAGE_TAKE_FACE))
    {
        App_RequestRedraw(APP_REDRAW_FACE_UI);
    }
}

static void App_OnEnrollDone(int16_t face_id, uint8_t direction)
{
    (void)direction;
    s_enrollFaceId = face_id;
    s_enrollDoneFlag = 1U;
}

static void App_OnEnrollFail(uint8_t error)
{
    s_lastFaceError = error;
    s_enrollFailFlag = 1U;
}

static void App_ProcessFaceEvents(void)
{
    if ((s_page == PAGE_STORE_FACE) && (s_enrollDoneFlag != 0U))
    {
        s_enrollDoneFlag = 0U;
        s_store.slots[s_pendingLocker - 1U].occupied = 1U;
        s_store.slots[s_pendingLocker - 1U].face_id = s_enrollFaceId;
        App_ResetFaceUi();
        App_SaveStorage();
        App_StartDoorOpen(s_pendingLocker, OPEN_STORE);
    }
    else if ((s_page == PAGE_STORE_FACE) && (s_enrollFailFlag != 0U))
    {
        char msg[40];
        s_enrollFailFlag = 0U;
        memset(&s_store.slots[s_pendingLocker - 1U], 0, sizeof(s_store.slots[0]));
        App_ResetFaceUi();
        (void)snprintf(msg, sizeof(msg), "Face fail:%s", App_GetFaceErrorText(s_lastFaceError));
        App_ShowMessage(msg, PAGE_HOME, 1800U);
    }

    if ((s_page == PAGE_TAKE_FACE) && (s_faceMatchedFlag != 0U))
    {
        uint8_t locker_id;
        s_faceMatchedFlag = 0U;
        locker_id = App_FindLockerByFace(s_faceMatchedId);
        if (locker_id != 0U)
        {
            App_ResetFaceUi();
            App_StartDoorOpen(locker_id, OPEN_TAKE);
        }
        else
        {
            App_ClearInput();
            App_ResetFaceUi();
            App_ShowMessage("No locker", PAGE_TAKE_CABINET, 1200U);
        }
    }
    else if ((s_page == PAGE_TAKE_FACE) && ((s_faceUnmatchedFlag != 0U) || (s_faceInvalidFlag != 0U)))
    {
        uint8_t error = s_lastFaceError;

        s_faceUnmatchedFlag = 0U;
        s_faceInvalidFlag = 0U;
        App_ResetFaceUi();
        if ((error == FM225_ERR_CAMERA) ||
            (error == FM225_ERR_INVALID_PARAM) ||
            (error == FM225_ERR_UNKNOWN_USER))
        {
            App_StartTakePassword();
        }
        else
        {
            FM225_VerifyFace();
            App_RequestRedraw(APP_REDRAW_FACE_UI);
        }
    }
}

static void App_HandleMenuKey(KeyEvent_t key)
{
    uint8_t count = (uint8_t)(sizeof(s_mainMenu) / sizeof(s_mainMenu[0]));

    if (key == KEY_UP)
    {
        s_prevMenuIndex = s_menuIndex;
        s_menuIndex = (s_menuIndex == 0U) ? (uint8_t)(count - 1U) : (uint8_t)(s_menuIndex - 1U);
        App_RequestRedraw(APP_REDRAW_MENU_SELECT);
    }
    else if (key == KEY_DOWN)
    {
        s_prevMenuIndex = s_menuIndex;
        s_menuIndex = (uint8_t)((s_menuIndex + 1U) % count);
        App_RequestRedraw(APP_REDRAW_MENU_SELECT);
    }
    else if (key == KEY_BACK)
    {
        App_SetPage(PAGE_HOME);
    }
    else if (key == KEY_ENTER)
    {
        if (s_menuIndex == 0U)
        {
            App_StartStore();
        }
        else if (s_menuIndex == 1U)
        {
            App_StartTake();
        }
        else if (s_menuIndex == 2U)
        {
            App_ClearInput();
            App_SetPage(PAGE_ADMIN_PASS);
        }
        else
        {
            s_recordsReturn = PAGE_MENU;
            App_SetPage(PAGE_RECORDS);
        }
    }
}

static void App_HandleAdminMenuKey(KeyEvent_t key)
{
    uint8_t count = (uint8_t)(sizeof(s_adminMenu) / sizeof(s_adminMenu[0]));

    if (key == KEY_UP)
    {
        s_prevAdminIndex = s_adminIndex;
        s_adminIndex = (s_adminIndex == 0U) ? (uint8_t)(count - 1U) : (uint8_t)(s_adminIndex - 1U);
        App_RequestRedraw(APP_REDRAW_MENU_SELECT);
    }
    else if (key == KEY_DOWN)
    {
        s_prevAdminIndex = s_adminIndex;
        s_adminIndex = (uint8_t)((s_adminIndex + 1U) % count);
        App_RequestRedraw(APP_REDRAW_MENU_SELECT);
    }
    else if (key == KEY_BACK)
    {
        App_SetPage(PAGE_HOME);
    }
    else if (key == KEY_ENTER)
    {
        if (s_adminIndex == 0U)
        {
            s_selectedLocker = 1U;
            App_SetPage(PAGE_ADMIN_OPEN);
        }
        else if (s_adminIndex == 1U)
        {
            s_recordsReturn = PAGE_ADMIN_MENU;
            App_SetPage(PAGE_RECORDS);
        }
        else if (s_adminIndex == 2U)
        {
            s_localExportRequest = 1U;
            App_ShowMessage("Export start", PAGE_ADMIN_MENU, 1000U);
        }
        else if (s_adminIndex == 3U)
        {
            s_store.alarm = 0U;
            Buzzer_Off();
            App_SaveStorage();
            App_RequestNetworkStatus();
            App_ShowMessage("Alarm cleared", PAGE_ADMIN_MENU, 1000U);
        }
        else
        {
            App_ClearAllLockers();
        }
    }
}

static void App_HandleKey(KeyEvent_t key)
{
    if (key == KEY_NONE)
    {
        return;
    }

    switch (s_page)
    {
    case PAGE_HOME:
        if (key == KEY_ENTER)
        {
            s_menuIndex = 0U;
            App_SetPage(PAGE_MENU);
        }
        break;
    case PAGE_MENU:
        App_HandleMenuKey(key);
        break;
    case PAGE_STORE_PASS:
    case PAGE_TAKE_CABINET:
    case PAGE_TAKE_PASS:
    case PAGE_ADMIN_PASS:
        if (key == KEY_BACK)
        {
            App_SetPage(PAGE_MENU);
        }
        break;
    case PAGE_STORE_FACE:
        if (key == KEY_BACK)
        {
            FM225_Abort();
            App_ResetFaceUi();
            memset(&s_store.slots[s_pendingLocker - 1U], 0, sizeof(s_store.slots[0]));
            App_SetPage(PAGE_MENU);
        }
        break;
    case PAGE_TAKE_FACE:
        if (key == KEY_BACK)
        {
            FM225_Abort();
            App_ResetFaceUi();
            App_StartTakePassword();
        }
        break;
    case PAGE_ADMIN_MENU:
        App_HandleAdminMenuKey(key);
        break;
    case PAGE_ADMIN_OPEN:
        if (key == KEY_LEFT)
        {
            s_selectedLocker = (s_selectedLocker == 1U) ? LOCKER_COUNT : (uint8_t)(s_selectedLocker - 1U);
            App_RequestRedraw(APP_REDRAW_ADMIN_LOCKER);
        }
        else if (key == KEY_RIGHT)
        {
            s_selectedLocker = (s_selectedLocker == LOCKER_COUNT) ? 1U : (uint8_t)(s_selectedLocker + 1U);
            App_RequestRedraw(APP_REDRAW_ADMIN_LOCKER);
        }
        else if (key == KEY_ENTER)
        {
            if (App_StartDoorOpen(s_selectedLocker, OPEN_ADMIN) == 0U)
            {
                App_ShowMessage("Door busy", PAGE_ADMIN_OPEN, 800U);
            }
        }
        else if (key == KEY_BACK)
        {
            App_SetPage(PAGE_ADMIN_MENU);
        }
        break;
    case PAGE_RECORDS:
        if (key == KEY_ENTER)
        {
            s_localExportRequest = 1U;
            App_ShowMessage("Export start", s_recordsReturn, 1000U);
        }
        else if (key == KEY_BACK)
        {
            App_SetPage(s_recordsReturn);
        }
        break;
    case PAGE_DOOR_OPEN:
        if (key == KEY_ENTER)
        {
            App_CloseDoor();
        }
        break;
    case PAGE_MESSAGE:
        if (key == KEY_BACK)
        {
            App_SetPage(s_messageNext);
        }
        break;
    default:
        if (key == KEY_BACK)
        {
            App_SetPage(PAGE_HOME);
        }
        break;
    }
}

static void App_HandleMatrixKey(char key)
{
    if (key == '\0')
    {
        return;
    }

    switch (s_page)
    {
    case PAGE_STORE_PASS:
        App_HandlePasswordKey(key);
        if ((key == '#') && (s_inputLen == LOCKER_PASSWORD_LEN))
        {
            App_ConfirmStorePassword();
        }
        break;
    case PAGE_TAKE_CABINET:
        if ((key >= '0') && (key <= '9') && (s_inputLen < 2U))
        {
            s_input[s_inputLen++] = key;
            s_input[s_inputLen] = '\0';
            App_RequestRedraw(APP_REDRAW_INPUT_VALUE);
        }
        else if ((key == '*') && (s_inputLen > 0U))
        {
            s_input[--s_inputLen] = '\0';
            App_RequestRedraw(APP_REDRAW_INPUT_VALUE);
        }
        else if ((key == '#') && (s_inputLen > 0U))
        {
            App_ConfirmTakeLocker();
        }
        break;
    case PAGE_TAKE_PASS:
        App_HandlePasswordKey(key);
        if ((key == '#') && (s_inputLen == LOCKER_PASSWORD_LEN))
        {
            App_ConfirmTakePassword();
        }
        break;
    case PAGE_ADMIN_PASS:
        App_HandlePasswordKey(key);
        if ((key == '#') && (s_inputLen == LOCKER_PASSWORD_LEN))
        {
            App_ConfirmAdminPassword();
        }
        break;
    default:
        break;
    }
}

void LockerApp_Init(void)
{
    PCF8563_Time_t now;
    PCF8563_Time_t build_time;

    Relay_Init();
    Buzzer_Init();
    AT24C256_Init();
    PCF8563_Init();
    if ((PCF8563_GetTime(&now) != HAL_OK) || (now.valid == 0U))
    {
        App_FillTimeFromBuild(&build_time);
        (void)PCF8563_SetTime(&build_time);
    }
    ST7735_Init();
    MatrixKeypad_Init();
    Key_Init();
    FM225_Init();
    ESP_Init();
    ESP_RegisterCallback(App_OnTcpPacket);
    FM225_SetMatchedCallback(App_OnFaceMatched);
    FM225_SetUnmatchedCallback(App_OnFaceUnmatched);
    FM225_SetInvalidCallback(App_OnFaceInvalid);
    FM225_SetFaceInfoCallback(App_OnFaceInfo);
    FM225_SetEnrollDoneCallback(App_OnEnrollDone);
    FM225_SetEnrollFailCallback(App_OnEnrollFail);

    App_LoadStorage();
    Relay_AllOff();
    if (s_store.alarm != 0U)
    {
        Buzzer_On();
    }
    App_ResetFaceUi();
    App_RequestRedraw(APP_REDRAW_FULL);
}

void LockerApp_Process(void)
{
    KeyEvent_t key;
    char matrixKey;
    uint32_t now = HAL_GetTick();

    App_ProcessNetwork();
    FM225_Process();
    App_ProcessFaceEvents();

    key = Key_Scan();
    matrixKey = MatrixKeypad_GetKey();
    App_HandleKey(key);
    App_HandleMatrixKey(matrixKey);
    App_ProcessFaceUi(now);
    now = HAL_GetTick();

    if ((s_page == PAGE_DOOR_OPEN) && (s_doorOpen != 0U))
    {
        if ((s_store.alarm == 0U) && ((now - s_doorOpenTick) >= LOCKER_DOOR_TIMEOUT_MS))
        {
            s_store.alarm = 1U;
            Buzzer_On();
            App_AddRecord(5U, s_selectedLocker, -1, 0U);
            App_RequestNetworkStatus();
            App_RequestRedraw(APP_REDRAW_DOOR_TIMER | APP_REDRAW_DOOR_ALARM);
        }
        if ((now - s_lastHomeTick) >= 1000U)
        {
            s_lastHomeTick = now;
            App_RequestRedraw(APP_REDRAW_DOOR_TIMER);
        }
    }

    if ((s_page == PAGE_HOME) && ((now - s_lastHomeTick) >= 1000U))
    {
        s_lastHomeTick = now;
        App_RequestRedraw(APP_REDRAW_HOME_STATUS);
    }

    if ((s_page == PAGE_MESSAGE) && ((int32_t)(now - s_messageUntil) >= 0))
    {
        App_SetPage(s_messageNext);
    }

    App_Draw();
}
