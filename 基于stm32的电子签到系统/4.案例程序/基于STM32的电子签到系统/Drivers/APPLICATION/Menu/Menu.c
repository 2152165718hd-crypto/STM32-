#include "APPLICATION/Menu/Menu.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define ATTENDANCE_STORAGE_MAGIC 0x4154544Eu
#define ATTENDANCE_STORAGE_VERSION 0x0001u
#define ATTENDANCE_STORAGE_HEADER_ADDR 0x0000u
#define ATTENDANCE_STORAGE_AUTH_ADDR 0x0040u
#define ATTENDANCE_STORAGE_RECORD_ADDR 0x0CC0u

#define ATTENDANCE_LED_PORT GPIOB
#define ATTENDANCE_LED_PIN GPIO_PIN_0
#define ATTENDANCE_LED_ON_STATE GPIO_PIN_RESET
#define ATTENDANCE_LED_OFF_STATE GPIO_PIN_SET

#define ATTENDANCE_TEXT_BUF_SIZE 32u
#define ATTENDANCE_STATUS_TIMEOUT_MS 1500u
#define ATTENDANCE_LED_TIMEOUT_MS 1500u
#define ATTENDANCE_BUZZER_TIMEOUT_MS 120u
#define ATTENDANCE_CARD_POLL_MS 120u
#define ATTENDANCE_TIME_REFRESH_MS 200u
#define ATTENDANCE_DISPLAY_REFRESH_MS 100u
#define ATTENDANCE_ROTATE_MS 2000u
#define ATTENDANCE_NO_CARD_RELEASE_COUNT 2u
#define ATTENDANCE_TCP_RECORDS_MAX 20u
#define ATTENDANCE_TCP_MAX_LINKS 5u
#define ATTENDANCE_INVALID_INDEX 0xFFFFu

typedef struct
{
    uint8_t open;
    uint8_t waiting_card;
    int16_t existing_index;
    uint8_t cursor;
    RC522_CardInfo_t card;
    char person_id[ATTENDANCE_PERSON_ID_LEN + 1u];
} AttendanceEnrollState_t;

typedef struct
{
    uint8_t open;
    uint8_t field_index;
    DS1302_Time_t edit_time;
} AttendanceTimeEditState_t;

typedef enum
{
    ATTENDANCE_CARD_PAGE_LIBRARY = 0,
    ATTENDANCE_CARD_PAGE_DELETE_ONE,
    ATTENDANCE_CARD_PAGE_DELETE_ALL
} AttendanceCardPageMode_t;

typedef struct
{
    uint8_t open;
    AttendanceCardPageMode_t mode;
    uint8_t confirm;
    uint16_t logical_index;
    uint16_t physical_index;
} AttendanceCardManageState_t;

typedef struct
{
    AttendanceStorageHeader_t header;
    AttendanceAuthorizedCard_t authorized[ATTENDANCE_MAX_AUTH_CARDS];
    AttendanceRecord_t latest_record;
    DS1302_Time_t current_time;
    AttendanceDisplayMode_t view;
    AttendanceStatus_t status;
    AttendanceEnrollState_t enroll;
    AttendanceTimeEditState_t time_edit;
    AttendanceCardManageState_t card_manage;
    MF_Menu_t *menu_root;
    MF_Menu_t *menu_rfid;
    MF_Menu_t *menu_delete_card;
    uint8_t has_latest_record;
    uint8_t storage_ready;
    uint8_t wifi_ready;
    uint8_t rtc_valid;
    uint8_t combo_latched;
    uint8_t card_latched;
    uint8_t no_card_counter;
    RC522_CardInfo_t latched_card;
    uint16_t browse_index;
    uint16_t rotate_offset;
    uint32_t next_poll_tick;
    uint32_t next_time_tick;
    uint32_t next_display_tick;
    uint32_t next_rotate_tick;
    uint32_t buzzer_deadline;
    uint32_t led_deadline;
    uint32_t temp_status_deadline;
    uint8_t tcp_link_mask;
    char temp_status[ATTENDANCE_TEXT_BUF_SIZE];
    char protocol_buffer[ESP_TX_BUF_SIZE];
} AttendanceAppState_t;

static AttendanceAppState_t g_app;

static void Attendance_LED_Init(void);
static void Attendance_LED_On(void);
static void Attendance_LED_Off(void);
static void Attendance_SetTemporaryStatus(AttendanceStatus_t status, const char *text, uint32_t timeout_ms);
static const char *Attendance_GetStatusText(void);
static void Attendance_StartBuzzer(void);
static void Attendance_StartInvalidAlarm(void);
static void Attendance_UpdateIndicators(void);

static uint16_t Attendance_GetAuthAddress(uint16_t index);
static uint16_t Attendance_GetRecordAddress(uint16_t index);
static void Attendance_ResetHeader(void);
static HAL_StatusTypeDef Attendance_SaveHeader(void);
static HAL_StatusTypeDef Attendance_LoadStorage(void);
static HAL_StatusTypeDef Attendance_ReadRecordPhysical(uint16_t physical_index, AttendanceRecord_t *record);
static uint16_t Attendance_GetRecordPhysicalIndex(uint16_t logical_index);
static HAL_StatusTypeDef Attendance_ReadRecordLogical(uint16_t logical_index, AttendanceRecord_t *record);
static void Attendance_LoadLatestRecord(void);
static int16_t Attendance_FindAuthorizedIndex(const RC522_CardInfo_t *card);
static uint16_t Attendance_CountAuthorizedCards(void);
static uint16_t Attendance_GetAuthorizedPhysicalIndex(uint16_t logical_index);
static HAL_StatusTypeDef Attendance_SaveAuthorizedEntry(uint16_t index);
static HAL_StatusTypeDef Attendance_DeleteAuthorizedEntry(uint16_t physical_index);
static HAL_StatusTypeDef Attendance_ClearAllAuthorizedCards(void);
static void Attendance_CopyPersonId(char *dst, const char *src);
static void Attendance_DefaultPersonId(char *dst);
static uint8_t Attendance_AddOrUpdateAuthorizedCard(const RC522_CardInfo_t *card, const char *person_id);
static HAL_StatusTypeDef Attendance_ClearAllRecords(void);
static HAL_StatusTypeDef Attendance_AppendRecord(const RC522_CardInfo_t *card, const AttendanceAuthorizedCard_t *auth);

static uint8_t Attendance_DaysInMonth(uint8_t year, uint8_t month);
static uint8_t Attendance_CalcWeekday(uint8_t year, uint8_t month, uint8_t day);
static uint8_t Attendance_IsTimeValid(const DS1302_Time_t *time);
static void Attendance_UpdateCurrentTime(void);
static void Attendance_FormatDateTime(const DS1302_Time_t *time, char *buf, uint16_t len);
static void Attendance_FormatRecordTime(const AttendanceRecord_t *record, char *buf, uint16_t len);
static void Attendance_AdjustTimeField(int8_t delta);

static uint8_t Attendance_CompareCard(const RC522_CardInfo_t *a, const RC522_CardInfo_t *b);
static void Attendance_HandleAuthorizedCard(const RC522_CardInfo_t *card);
static void Attendance_PollAttendanceCard(void);
static void Attendance_PollEnrollCard(void);

static void Attendance_BuildMenu(void);
static void Attendance_EnterMenu(void);
static void Attendance_LeaveMenu(void);
static void Attendance_OpenEnrollPage(void);
static void Attendance_OpenTimePageDirect(void);
static void Attendance_ClearRecordsAction(void);
static void Attendance_OpenCardLibraryPage(void);
static void Attendance_OpenDeleteOnePage(void);
static void Attendance_OpenDeleteAllPage(void);
static void Attendance_EnrollPage(KeyEvent_t key, uint8_t *exit_flag);
static void Attendance_TimePage(KeyEvent_t key, uint8_t *exit_flag);
static void Attendance_InfoPage(KeyEvent_t key, uint8_t *exit_flag);
static void Attendance_CardLibraryPage(KeyEvent_t key, uint8_t *exit_flag);
static void Attendance_DeleteOnePage(KeyEvent_t key, uint8_t *exit_flag);
static void Attendance_DeleteAllPage(KeyEvent_t key, uint8_t *exit_flag);
static void Attendance_RenderHome(void);
static void Attendance_RenderRecords(void);
static void Attendance_RenderEnrollPage(void);
static void Attendance_RenderTimePage(void);
static void Attendance_RenderInfoPage(void);
static void Attendance_RenderCardManagePage(void);
static void Attendance_RenderDeleteAllPage(void);
static void Attendance_RefreshBrowseIndex(void);
static void Attendance_RefreshCardManageIndex(void);
static void Attendance_FormatUidSummary(const AttendanceAuthorizedCard_t *entry, char *buf, uint16_t len);
static void Attendance_RotateHomeRecord(void);
static void Attendance_ProcessInput(void);

static void Attendance_TcpCallback(ESP_DataPacket_t *packet);
static void Attendance_AppendFormat(char *buf, uint16_t *used, uint16_t size, const char *fmt, ...);
static const char *Attendance_GetProtocolStatusText(void);
static void Attendance_FormatDateTimeTcp(const DS1302_Time_t *time, char *buf, uint16_t len);
static void Attendance_FormatRecordTimeTcp(const AttendanceRecord_t *record, char *buf, uint16_t len);
static void Attendance_TrimTcpCommand(char *cmd);
static uint16_t Attendance_ParseRecordLimit(const char *cmd);
static void Attendance_MarkTcpLink(uint8_t link_id);
static void Attendance_ClearTcpLink(uint8_t link_id);
static uint8_t Attendance_SendTcpText(uint8_t link_id, const char *text);
static uint8_t Attendance_SendTcpLine(uint8_t link_id, const char *fmt, ...);
static void Attendance_SendTcpStatus(uint8_t link_id);
static void Attendance_SendTcpLatest(uint8_t link_id);
static void Attendance_SendTcpRecords(uint8_t link_id, uint16_t requested_count);
static void Attendance_PushSignOkEvent(const AttendanceRecord_t *record);

static void Attendance_LED_Init(void)
{
    GPIO_InitTypeDef gpio_init;

    __HAL_RCC_GPIOB_CLK_ENABLE();
    memset(&gpio_init, 0, sizeof(gpio_init));
    gpio_init.Pin = ATTENDANCE_LED_PIN;
    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(ATTENDANCE_LED_PORT, &gpio_init);
    Attendance_LED_Off();
}

static void Attendance_LED_On(void)
{
    HAL_GPIO_WritePin(ATTENDANCE_LED_PORT, ATTENDANCE_LED_PIN, ATTENDANCE_LED_ON_STATE);
}

static void Attendance_LED_Off(void)
{
    HAL_GPIO_WritePin(ATTENDANCE_LED_PORT, ATTENDANCE_LED_PIN, ATTENDANCE_LED_OFF_STATE);
}

static void Attendance_SetTemporaryStatus(AttendanceStatus_t status, const char *text, uint32_t timeout_ms)
{
    uint32_t now;

    now = HAL_GetTick();
    g_app.status = status;
    g_app.temp_status_deadline = now + timeout_ms;
    memset(g_app.temp_status, 0, sizeof(g_app.temp_status));

    if (text != NULL)
    {
        strncpy(g_app.temp_status, text, sizeof(g_app.temp_status) - 1u);
    }
}

static const char *Attendance_GetStatusText(void)
{
    uint32_t now;
    static char wifi_status[16];

    now = HAL_GetTick();
    if ((g_app.temp_status[0] != '\0') && ((int32_t)(g_app.temp_status_deadline - now) > 0))
    {
        return g_app.temp_status;
    }

    if (g_app.storage_ready == 0u)
    {
        return "EEPROM ERR";
    }

    if (g_app.rtc_valid == 0u)
    {
        return "SET TIME";
    }

    if (g_app.wifi_ready == 0u)
    {
        (void)snprintf(wifi_status, sizeof(wifi_status), "WIFI E%02u", (unsigned int)ESP_GetLastErrorStep());
        return wifi_status;
    }

    return "WAIT CARD";
}

static void Attendance_StartBuzzer(void)
{
    Buzzer_On();
    g_app.buzzer_deadline = HAL_GetTick() + ATTENDANCE_BUZZER_TIMEOUT_MS;
}

static void Attendance_StartInvalidAlarm(void)
{
    Attendance_LED_On();
    g_app.led_deadline = HAL_GetTick() + ATTENDANCE_LED_TIMEOUT_MS;
    Attendance_SetTemporaryStatus(ATTENDANCE_STATUS_INVALID_CARD, "INVALID CARD", ATTENDANCE_STATUS_TIMEOUT_MS);
}

static void Attendance_UpdateIndicators(void)
{
    uint32_t now;

    now = HAL_GetTick();
    if ((g_app.buzzer_deadline != 0u) && ((int32_t)(now - g_app.buzzer_deadline) >= 0))
    {
        Buzzer_Off();
        g_app.buzzer_deadline = 0u;
    }

    if ((g_app.led_deadline != 0u) && ((int32_t)(now - g_app.led_deadline) >= 0))
    {
        Attendance_LED_Off();
        g_app.led_deadline = 0u;
    }

    if ((g_app.temp_status[0] != '\0') && ((int32_t)(now - g_app.temp_status_deadline) >= 0))
    {
        g_app.temp_status[0] = '\0';
        g_app.status = ATTENDANCE_STATUS_WAIT_CARD;
    }
}

static uint16_t Attendance_GetAuthAddress(uint16_t index)
{
    return (uint16_t)(ATTENDANCE_STORAGE_AUTH_ADDR + (index * sizeof(AttendanceAuthorizedCard_t)));
}

static uint16_t Attendance_GetRecordAddress(uint16_t index)
{
    return (uint16_t)(ATTENDANCE_STORAGE_RECORD_ADDR + (index * sizeof(AttendanceRecord_t)));
}

static void Attendance_ResetHeader(void)
{
    memset(&g_app.header, 0, sizeof(g_app.header));
    g_app.header.magic = ATTENDANCE_STORAGE_MAGIC;
    g_app.header.version = ATTENDANCE_STORAGE_VERSION;
    g_app.header.last_index = ATTENDANCE_INVALID_INDEX;
}

static HAL_StatusTypeDef Attendance_SaveHeader(void)
{
    return AT24C256_Write(ATTENDANCE_STORAGE_HEADER_ADDR,
                          (const uint8_t *)&g_app.header,
                          (uint16_t)sizeof(g_app.header));
}

static uint8_t Attendance_IsValidStoredCard(const AttendanceAuthorizedCard_t *entry)
{
    if (entry == NULL)
    {
        return 0u;
    }

    if (entry->valid != 1u)
    {
        return 0u;
    }

    if ((entry->uid_len != 4u) && (entry->uid_len != 7u) && (entry->uid_len != 10u))
    {
        return 0u;
    }

    return 1u;
}

static HAL_StatusTypeDef Attendance_ClearAuthorizedTable(void)
{
    AttendanceAuthorizedCard_t empty_entry;
    uint16_t index;

    memset(&empty_entry, 0, sizeof(empty_entry));
    for (index = 0u; index < ATTENDANCE_MAX_AUTH_CARDS; index++)
    {
        if (AT24C256_Write(Attendance_GetAuthAddress(index),
                           (const uint8_t *)&empty_entry,
                           (uint16_t)sizeof(empty_entry)) != HAL_OK)
        {
            return HAL_ERROR;
        }
    }

    return HAL_OK;
}

static HAL_StatusTypeDef Attendance_LoadStorage(void)
{
    AttendanceStorageHeader_t header;
    uint16_t index;
    uint16_t valid_count;
    uint8_t reset_storage;

    g_app.storage_ready = 0u;
    g_app.has_latest_record = 0u;
    memset(g_app.authorized, 0, sizeof(g_app.authorized));

    if (AT24C256_IsReady() != HAL_OK)
    {
        return HAL_ERROR;
    }

    reset_storage = 0u;
    if (AT24C256_Read(ATTENDANCE_STORAGE_HEADER_ADDR,
                      (uint8_t *)&header,
                      (uint16_t)sizeof(header)) != HAL_OK)
    {
        return HAL_ERROR;
    }

    if ((header.magic != ATTENDANCE_STORAGE_MAGIC) ||
        (header.version != ATTENDANCE_STORAGE_VERSION) ||
        (header.authorized_count > ATTENDANCE_MAX_AUTH_CARDS) ||
        (header.record_count > ATTENDANCE_MAX_RECORDS) ||
        (header.write_index >= ATTENDANCE_MAX_RECORDS))
    {
        reset_storage = 1u;
    }

    if (reset_storage != 0u)
    {
        Attendance_ResetHeader();
        if (Attendance_ClearAuthorizedTable() != HAL_OK)
        {
            return HAL_ERROR;
        }
        if (Attendance_SaveHeader() != HAL_OK)
        {
            return HAL_ERROR;
        }
        g_app.storage_ready = 1u;
        return HAL_OK;
    }

    g_app.header = header;
    valid_count = 0u;
    for (index = 0u; index < ATTENDANCE_MAX_AUTH_CARDS; index++)
    {
        if (AT24C256_Read(Attendance_GetAuthAddress(index),
                          (uint8_t *)&g_app.authorized[index],
                          (uint16_t)sizeof(g_app.authorized[index])) != HAL_OK)
        {
            return HAL_ERROR;
        }

        if (Attendance_IsValidStoredCard(&g_app.authorized[index]) != 0u)
        {
            valid_count++;
            g_app.authorized[index].person_id[ATTENDANCE_PERSON_ID_LEN] = '\0';
        }
        else
        {
            memset(&g_app.authorized[index], 0, sizeof(g_app.authorized[index]));
        }
    }

    if (g_app.header.authorized_count != valid_count)
    {
        g_app.header.authorized_count = valid_count;
        (void)Attendance_SaveHeader();
    }

    if (g_app.header.last_index >= ATTENDANCE_MAX_RECORDS)
    {
        g_app.header.last_index = ATTENDANCE_INVALID_INDEX;
    }

    Attendance_LoadLatestRecord();
    g_app.storage_ready = 1u;
    return HAL_OK;
}

static HAL_StatusTypeDef Attendance_ReadRecordPhysical(uint16_t physical_index, AttendanceRecord_t *record)
{
    if ((record == NULL) || (physical_index >= ATTENDANCE_MAX_RECORDS))
    {
        return HAL_ERROR;
    }

    if (AT24C256_Read(Attendance_GetRecordAddress(physical_index),
                      (uint8_t *)record,
                      (uint16_t)sizeof(*record)) != HAL_OK)
    {
        return HAL_ERROR;
    }

    if ((record->valid != 1u) ||
        ((record->uid_len != 4u) && (record->uid_len != 7u) && (record->uid_len != 10u)))
    {
        return HAL_ERROR;
    }

    record->person_id[ATTENDANCE_PERSON_ID_LEN] = '\0';
    return HAL_OK;
}

static uint16_t Attendance_GetRecordPhysicalIndex(uint16_t logical_index)
{
    uint16_t physical_index;

    if ((g_app.header.record_count == 0u) || (logical_index >= g_app.header.record_count))
    {
        return ATTENDANCE_INVALID_INDEX;
    }

    if (g_app.header.write_index >= ATTENDANCE_MAX_RECORDS)
    {
        return ATTENDANCE_INVALID_INDEX;
    }

    physical_index = (uint16_t)((g_app.header.write_index + ATTENDANCE_MAX_RECORDS - 1u) % ATTENDANCE_MAX_RECORDS);
    while (logical_index > 0u)
    {
        if (physical_index == 0u)
        {
            physical_index = ATTENDANCE_MAX_RECORDS - 1u;
        }
        else
        {
            physical_index--;
        }
        logical_index--;
    }

    return physical_index;
}

static HAL_StatusTypeDef Attendance_ReadRecordLogical(uint16_t logical_index, AttendanceRecord_t *record)
{
    uint16_t physical_index;

    physical_index = Attendance_GetRecordPhysicalIndex(logical_index);
    if (physical_index == ATTENDANCE_INVALID_INDEX)
    {
        return HAL_ERROR;
    }

    return Attendance_ReadRecordPhysical(physical_index, record);
}

static void Attendance_LoadLatestRecord(void)
{
    if ((g_app.header.record_count == 0u) ||
        (g_app.header.last_index == ATTENDANCE_INVALID_INDEX) ||
        (g_app.header.last_index >= ATTENDANCE_MAX_RECORDS))
    {
        g_app.has_latest_record = 0u;
        memset(&g_app.latest_record, 0, sizeof(g_app.latest_record));
        return;
    }

    if (Attendance_ReadRecordPhysical(g_app.header.last_index, &g_app.latest_record) == HAL_OK)
    {
        g_app.has_latest_record = 1u;
    }
    else
    {
        g_app.has_latest_record = 0u;
        memset(&g_app.latest_record, 0, sizeof(g_app.latest_record));
    }
}

static uint8_t Attendance_CompareCard(const RC522_CardInfo_t *a, const RC522_CardInfo_t *b)
{
    if ((a == NULL) || (b == NULL))
    {
        return 0u;
    }

    if ((a->uid_len == 0u) || (a->uid_len > RC522_UID_MAX_LEN) || (a->uid_len != b->uid_len))
    {
        return 0u;
    }

    return (uint8_t)(memcmp(a->uid, b->uid, a->uid_len) == 0);
}

static int16_t Attendance_FindAuthorizedIndex(const RC522_CardInfo_t *card)
{
    uint16_t index;

    if (card == NULL)
    {
        return -1;
    }

    for (index = 0u; index < ATTENDANCE_MAX_AUTH_CARDS; index++)
    {
        if ((g_app.authorized[index].valid == 1u) &&
            (g_app.authorized[index].uid_len == card->uid_len) &&
            (memcmp(g_app.authorized[index].uid, card->uid, card->uid_len) == 0))
        {
            return (int16_t)index;
        }
    }

    return -1;
}

static uint16_t Attendance_CountAuthorizedCards(void)
{
    uint16_t index;
    uint16_t count;

    count = 0u;
    for (index = 0u; index < ATTENDANCE_MAX_AUTH_CARDS; index++)
    {
        if (Attendance_IsValidStoredCard(&g_app.authorized[index]) != 0u)
        {
            count++;
        }
    }

    return count;
}

static uint16_t Attendance_GetAuthorizedPhysicalIndex(uint16_t logical_index)
{
    uint16_t index;
    uint16_t count;

    count = 0u;
    for (index = 0u; index < ATTENDANCE_MAX_AUTH_CARDS; index++)
    {
        if (Attendance_IsValidStoredCard(&g_app.authorized[index]) != 0u)
        {
            if (count == logical_index)
            {
                return index;
            }
            count++;
        }
    }

    return ATTENDANCE_INVALID_INDEX;
}

static HAL_StatusTypeDef Attendance_SaveAuthorizedEntry(uint16_t index)
{
    if (index >= ATTENDANCE_MAX_AUTH_CARDS)
    {
        return HAL_ERROR;
    }

    return AT24C256_Write(Attendance_GetAuthAddress(index),
                          (const uint8_t *)&g_app.authorized[index],
                          (uint16_t)sizeof(g_app.authorized[index]));
}

static HAL_StatusTypeDef Attendance_DeleteAuthorizedEntry(uint16_t physical_index)
{
    if ((physical_index >= ATTENDANCE_MAX_AUTH_CARDS) ||
        (g_app.storage_ready == 0u) ||
        (Attendance_IsValidStoredCard(&g_app.authorized[physical_index]) == 0u))
    {
        return HAL_ERROR;
    }

    memset(&g_app.authorized[physical_index], 0, sizeof(g_app.authorized[physical_index]));
    if (Attendance_SaveAuthorizedEntry(physical_index) != HAL_OK)
    {
        return HAL_ERROR;
    }

    g_app.header.authorized_count = Attendance_CountAuthorizedCards();
    return Attendance_SaveHeader();
}

static HAL_StatusTypeDef Attendance_ClearAllAuthorizedCards(void)
{
    if (g_app.storage_ready == 0u)
    {
        return HAL_ERROR;
    }

    if (Attendance_ClearAuthorizedTable() != HAL_OK)
    {
        return HAL_ERROR;
    }

    memset(g_app.authorized, 0, sizeof(g_app.authorized));
    g_app.header.authorized_count = 0u;
    return Attendance_SaveHeader();
}

static void Attendance_CopyPersonId(char *dst, const char *src)
{
    uint8_t index;

    if (dst == NULL)
    {
        return;
    }

    for (index = 0u; index < ATTENDANCE_PERSON_ID_LEN; index++)
    {
        if ((src != NULL) && (src[index] >= '0') && (src[index] <= '9'))
        {
            dst[index] = src[index];
        }
        else
        {
            dst[index] = '0';
        }
    }

    dst[ATTENDANCE_PERSON_ID_LEN] = '\0';
}

static void Attendance_DefaultPersonId(char *dst)
{
    Attendance_CopyPersonId(dst, "000");
}

static uint8_t Attendance_AddOrUpdateAuthorizedCard(const RC522_CardInfo_t *card, const char *person_id)
{
    int16_t existing_index;
    uint16_t index;
    uint16_t target_index;
    uint8_t is_update;

    if ((card == NULL) ||
        ((card->uid_len != 4u) && (card->uid_len != 7u) && (card->uid_len != 10u)) ||
        (g_app.storage_ready == 0u))
    {
        return 0u;
    }

    existing_index = Attendance_FindAuthorizedIndex(card);
    if (existing_index >= 0)
    {
        target_index = (uint16_t)existing_index;
        is_update = 1u;
    }
    else
    {
        is_update = 0u;
        target_index = ATTENDANCE_INVALID_INDEX;
        for (index = 0u; index < ATTENDANCE_MAX_AUTH_CARDS; index++)
        {
            if (g_app.authorized[index].valid != 1u)
            {
                target_index = index;
                break;
            }
        }
    }

    if (target_index == ATTENDANCE_INVALID_INDEX)
    {
        return 0u;
    }

    memset(&g_app.authorized[target_index], 0, sizeof(g_app.authorized[target_index]));
    g_app.authorized[target_index].valid = 1u;
    g_app.authorized[target_index].uid_len = card->uid_len;
    memcpy(g_app.authorized[target_index].uid, card->uid, card->uid_len);
    Attendance_CopyPersonId(g_app.authorized[target_index].person_id, person_id);

    if (Attendance_SaveAuthorizedEntry(target_index) != HAL_OK)
    {
        return 0u;
    }

    if (is_update == 0u)
    {
        g_app.header.authorized_count++;
    }

    if (Attendance_SaveHeader() != HAL_OK)
    {
        return 0u;
    }

    return (uint8_t)(is_update ? 2u : 1u);
}

static HAL_StatusTypeDef Attendance_ClearAllRecords(void)
{
    g_app.header.record_count = 0u;
    g_app.header.write_index = 0u;
    g_app.header.last_index = ATTENDANCE_INVALID_INDEX;
    g_app.has_latest_record = 0u;
    g_app.browse_index = 0u;
    g_app.rotate_offset = 0u;
    memset(&g_app.latest_record, 0, sizeof(g_app.latest_record));

    return Attendance_SaveHeader();
}

static HAL_StatusTypeDef Attendance_AppendRecord(const RC522_CardInfo_t *card, const AttendanceAuthorizedCard_t *auth)
{
    AttendanceRecord_t record;
    uint16_t physical_index;

    if ((card == NULL) || (auth == NULL) || (g_app.storage_ready == 0u) ||
        (g_app.header.write_index >= ATTENDANCE_MAX_RECORDS))
    {
        return HAL_ERROR;
    }

    memset(&record, 0, sizeof(record));
    record.valid = 1u;
    record.uid_len = card->uid_len;
    memcpy(record.uid, card->uid, card->uid_len);
    Attendance_CopyPersonId(record.person_id, auth->person_id);
    record.year = g_app.current_time.year;
    record.month = g_app.current_time.month;
    record.date = g_app.current_time.date;
    record.day = g_app.current_time.day;
    record.hour = g_app.current_time.hour;
    record.minute = g_app.current_time.minute;
    record.second = g_app.current_time.second;

    physical_index = g_app.header.write_index;
    if (AT24C256_Write(Attendance_GetRecordAddress(physical_index),
                       (const uint8_t *)&record,
                       (uint16_t)sizeof(record)) != HAL_OK)
    {
        return HAL_ERROR;
    }

    g_app.header.last_index = physical_index;
    g_app.header.write_index = (uint16_t)((physical_index + 1u) % ATTENDANCE_MAX_RECORDS);
    if (g_app.header.record_count < ATTENDANCE_MAX_RECORDS)
    {
        g_app.header.record_count++;
    }

    if (Attendance_SaveHeader() != HAL_OK)
    {
        return HAL_ERROR;
    }

    g_app.latest_record = record;
    g_app.has_latest_record = 1u;
    g_app.browse_index = 0u;
    g_app.rotate_offset = 0u;
    return HAL_OK;
}

static uint8_t Attendance_DaysInMonth(uint8_t year, uint8_t month)
{
    uint16_t full_year;

    if ((month == 1u) || (month == 3u) || (month == 5u) || (month == 7u) ||
        (month == 8u) || (month == 10u) || (month == 12u))
    {
        return 31u;
    }

    if ((month == 4u) || (month == 6u) || (month == 9u) || (month == 11u))
    {
        return 30u;
    }

    if (month == 2u)
    {
        full_year = (uint16_t)(2000u + year);
        if (((full_year % 400u) == 0u) ||
            (((full_year % 4u) == 0u) && ((full_year % 100u) != 0u)))
        {
            return 29u;
        }
        return 28u;
    }

    return 0u;
}

static uint8_t Attendance_CalcWeekday(uint8_t year, uint8_t month, uint8_t day)
{
    static const uint8_t month_table[12] = {0u, 3u, 2u, 5u, 0u, 3u, 5u, 1u, 4u, 6u, 2u, 4u};
    uint16_t full_year;
    uint8_t result;

    full_year = (uint16_t)(2000u + year);
    if (month < 3u)
    {
        full_year--;
    }

    result = (uint8_t)((full_year + (full_year / 4u) - (full_year / 100u) +
                        (full_year / 400u) + month_table[month - 1u] + day) %
                       7u);

    if (result == 0u)
    {
        return 7u;
    }

    return result;
}

static uint8_t Attendance_IsTimeValid(const DS1302_Time_t *time)
{
    uint8_t days;

    if (time == NULL)
    {
        return 0u;
    }

    if ((time->month < 1u) || (time->month > 12u) ||
        (time->hour > 23u) || (time->minute > 59u) || (time->second > 59u) ||
        (time->day < 1u) || (time->day > 7u))
    {
        return 0u;
    }

    days = Attendance_DaysInMonth(time->year, time->month);
    if ((days == 0u) || (time->date < 1u) || (time->date > days))
    {
        return 0u;
    }

    return 1u;
}

static void Attendance_UpdateCurrentTime(void)
{
    DS1302_Time_t now_time;

    memset(&now_time, 0, sizeof(now_time));
    DS1302_GetTime(&now_time);
    g_app.current_time = now_time;
    g_app.rtc_valid = Attendance_IsTimeValid(&now_time);
}

static void Attendance_FormatDateTime(const DS1302_Time_t *time, char *buf, uint16_t len)
{
    if ((time == NULL) || (buf == NULL) || (len == 0u))
    {
        return;
    }

    (void)snprintf(buf, len, "20%02u/%02u/%02u %02u:%02u:%02u",
                   (unsigned int)time->year,
                   (unsigned int)time->month,
                   (unsigned int)time->date,
                   (unsigned int)time->hour,
                   (unsigned int)time->minute,
                   (unsigned int)time->second);
}

static void Attendance_FormatRecordTime(const AttendanceRecord_t *record, char *buf, uint16_t len)
{
    if ((record == NULL) || (buf == NULL) || (len == 0u))
    {
        return;
    }

    (void)snprintf(buf, len, "20%02u/%02u/%02u %02u:%02u:%02u",
                   (unsigned int)record->year,
                   (unsigned int)record->month,
                   (unsigned int)record->date,
                   (unsigned int)record->hour,
                   (unsigned int)record->minute,
                   (unsigned int)record->second);
}

static void Attendance_AdjustTimeField(int8_t delta)
{
    DS1302_Time_t *time;
    uint8_t days;

    time = &g_app.time_edit.edit_time;

    switch (g_app.time_edit.field_index)
    {
    case 0u:
        if (delta > 0)
        {
            time->year = (uint8_t)((time->year >= 99u) ? 0u : (time->year + 1u));
        }
        else
        {
            time->year = (uint8_t)((time->year == 0u) ? 99u : (time->year - 1u));
        }
        break;

    case 1u:
        if (delta > 0)
        {
            time->month = (uint8_t)((time->month >= 12u) ? 1u : (time->month + 1u));
        }
        else
        {
            time->month = (uint8_t)((time->month <= 1u) ? 12u : (time->month - 1u));
        }
        break;

    case 2u:
        days = Attendance_DaysInMonth(time->year, time->month);
        if (delta > 0)
        {
            time->date = (uint8_t)((time->date >= days) ? 1u : (time->date + 1u));
        }
        else
        {
            time->date = (uint8_t)((time->date <= 1u) ? days : (time->date - 1u));
        }
        break;

    case 3u:
        if (delta > 0)
        {
            time->hour = (uint8_t)((time->hour >= 23u) ? 0u : (time->hour + 1u));
        }
        else
        {
            time->hour = (uint8_t)((time->hour == 0u) ? 23u : (time->hour - 1u));
        }
        break;

    case 4u:
        if (delta > 0)
        {
            time->minute = (uint8_t)((time->minute >= 59u) ? 0u : (time->minute + 1u));
        }
        else
        {
            time->minute = (uint8_t)((time->minute == 0u) ? 59u : (time->minute - 1u));
        }
        break;

    case 5u:
        if (delta > 0)
        {
            time->second = (uint8_t)((time->second >= 59u) ? 0u : (time->second + 1u));
        }
        else
        {
            time->second = (uint8_t)((time->second == 0u) ? 59u : (time->second - 1u));
        }
        break;

    default:
        break;
    }

    days = Attendance_DaysInMonth(time->year, time->month);
    if ((days > 0u) && (time->date > days))
    {
        time->date = days;
    }
    if (time->date == 0u)
    {
        time->date = 1u;
    }
}

static void Attendance_HandleAuthorizedCard(const RC522_CardInfo_t *card)
{
    int16_t index;

    if (card == NULL)
    {
        return;
    }

    if (g_app.storage_ready == 0u)
    {
        Attendance_SetTemporaryStatus(ATTENDANCE_STATUS_STORAGE_ERROR, "EEPROM ERR", ATTENDANCE_STATUS_TIMEOUT_MS);
        return;
    }

    if (g_app.rtc_valid == 0u)
    {
        Attendance_SetTemporaryStatus(ATTENDANCE_STATUS_TIME_INVALID, "SET TIME", ATTENDANCE_STATUS_TIMEOUT_MS);
        return;
    }

    index = Attendance_FindAuthorizedIndex(card);
    if (index < 0)
    {
        Attendance_StartInvalidAlarm();
        return;
    }

    if (Attendance_AppendRecord(card, &g_app.authorized[(uint16_t)index]) != HAL_OK)
    {
        Attendance_SetTemporaryStatus(ATTENDANCE_STATUS_STORAGE_ERROR, "EEPROM ERR", ATTENDANCE_STATUS_TIMEOUT_MS);
        return;
    }

    Attendance_StartBuzzer();
    Attendance_SetTemporaryStatus(ATTENDANCE_STATUS_SUCCESS, "SIGN OK", ATTENDANCE_STATUS_TIMEOUT_MS);
    Attendance_PushSignOkEvent(&g_app.latest_record);
}

static void Attendance_PollAttendanceCard(void)
{
    RC522_CardInfo_t card;
    RC522_Status_t status;
    uint32_t now;

    now = HAL_GetTick();
    if ((int32_t)(now - g_app.next_poll_tick) < 0)
    {
        return;
    }
    g_app.next_poll_tick = now + ATTENDANCE_CARD_POLL_MS;

    memset(&card, 0, sizeof(card));
    status = RC522_DetectCard(&card);
    if (status == RC522_NO_CARD)
    {
        if (g_app.no_card_counter < ATTENDANCE_NO_CARD_RELEASE_COUNT)
        {
            g_app.no_card_counter++;
        }

        if (g_app.no_card_counter >= ATTENDANCE_NO_CARD_RELEASE_COUNT)
        {
            g_app.card_latched = 0u;
            memset(&g_app.latched_card, 0, sizeof(g_app.latched_card));
        }
        return;
    }

    if (status != RC522_OK)
    {
        return;
    }

    g_app.no_card_counter = 0u;
    if (g_app.card_latched != 0u)
    {
        if (Attendance_CompareCard(&g_app.latched_card, &card) == 0u)
        {
            g_app.latched_card = card;
        }
        (void)RC522_HaltCard();
        return;
    }

    g_app.card_latched = 1u;
    g_app.latched_card = card;
    Attendance_HandleAuthorizedCard(&card);
    (void)RC522_HaltCard();
}

static void Attendance_PollEnrollCard(void)
{
    RC522_CardInfo_t card;
    RC522_Status_t status;
    int16_t existing_index;
    uint32_t now;

    if ((g_app.enroll.open == 0u) || (g_app.enroll.waiting_card == 0u))
    {
        return;
    }

    now = HAL_GetTick();
    if ((int32_t)(now - g_app.next_poll_tick) < 0)
    {
        return;
    }
    g_app.next_poll_tick = now + ATTENDANCE_CARD_POLL_MS;

    memset(&card, 0, sizeof(card));
    status = RC522_DetectCard(&card);
    if (status != RC522_OK)
    {
        return;
    }

    g_app.enroll.card = card;
    g_app.enroll.waiting_card = 0u;
    g_app.enroll.cursor = 0u;
    existing_index = Attendance_FindAuthorizedIndex(&card);
    g_app.enroll.existing_index = existing_index;
    if (existing_index >= 0)
    {
        Attendance_CopyPersonId(g_app.enroll.person_id,
                                g_app.authorized[(uint16_t)existing_index].person_id);
        Attendance_SetTemporaryStatus(ATTENDANCE_STATUS_CARD_SAVED, "EDIT OLD", ATTENDANCE_STATUS_TIMEOUT_MS);
    }
    else
    {
        Attendance_DefaultPersonId(g_app.enroll.person_id);
        Attendance_SetTemporaryStatus(ATTENDANCE_STATUS_CARD_SAVED, "CARD FOUND", ATTENDANCE_STATUS_TIMEOUT_MS);
    }

    (void)RC522_HaltCard();
}

static void Attendance_BuildMenu(void)
{
    MF_Reset();
    g_app.menu_root = MF_CreateMenu("Manage");
    g_app.menu_rfid = MF_CreateMenu("RFID Manage");
    g_app.menu_delete_card = MF_CreateMenu("Delete Card");
    if ((g_app.menu_root == NULL) ||
        (g_app.menu_rfid == NULL) ||
        (g_app.menu_delete_card == NULL))
    {
        return;
    }

    MF_AddSubmenu(g_app.menu_root, "RFID Manage", g_app.menu_rfid);
    MF_AddAction(g_app.menu_root, "Set Time", Attendance_OpenTimePageDirect);
    MF_AddAction(g_app.menu_root, "Clear Records", Attendance_ClearRecordsAction);
    MF_AddCustomPage(g_app.menu_root, "System Info", Attendance_InfoPage);

    MF_AddAction(g_app.menu_rfid, "Enroll Card", Attendance_OpenEnrollPage);
    MF_AddSubmenu(g_app.menu_rfid, "Delete Card", g_app.menu_delete_card);
    MF_AddAction(g_app.menu_rfid, "Card Library", Attendance_OpenCardLibraryPage);

    MF_AddAction(g_app.menu_delete_card, "Delete One", Attendance_OpenDeleteOnePage);
    MF_AddAction(g_app.menu_delete_card, "Delete All", Attendance_OpenDeleteAllPage);
    MF_SetCursorStyle(MF_CURSOR_REVERSE);
}

static void Attendance_EnterMenu(void)
{
    g_app.view = ATTENDANCE_VIEW_MENU;
    g_app.enroll.open = 0u;
    g_app.time_edit.open = 0u;
    g_app.card_manage.open = 0u;
    MF_Start(g_app.menu_root);
    Key_ClearEvents();
}

static void Attendance_LeaveMenu(void)
{
    g_app.view = ATTENDANCE_VIEW_HOME;
    g_app.enroll.open = 0u;
    g_app.time_edit.open = 0u;
    g_app.card_manage.open = 0u;
    Key_ClearEvents();
}

static void Attendance_OpenEnrollPage(void)
{
    memset(&g_app.enroll, 0, sizeof(g_app.enroll));
    g_app.enroll.open = 1u;
    g_app.enroll.waiting_card = 1u;
    g_app.enroll.existing_index = -1;
    Attendance_DefaultPersonId(g_app.enroll.person_id);
    MF_OpenCustomPage(Attendance_EnrollPage);
}

static void Attendance_OpenCardLibraryPage(void)
{
    memset(&g_app.card_manage, 0, sizeof(g_app.card_manage));
    g_app.card_manage.open = 1u;
    g_app.card_manage.mode = ATTENDANCE_CARD_PAGE_LIBRARY;
    g_app.card_manage.physical_index = ATTENDANCE_INVALID_INDEX;
    MF_OpenCustomPage(Attendance_CardLibraryPage);
}

static void Attendance_OpenDeleteOnePage(void)
{
    memset(&g_app.card_manage, 0, sizeof(g_app.card_manage));
    g_app.card_manage.open = 1u;
    g_app.card_manage.mode = ATTENDANCE_CARD_PAGE_DELETE_ONE;
    g_app.card_manage.physical_index = ATTENDANCE_INVALID_INDEX;
    MF_OpenCustomPage(Attendance_DeleteOnePage);
}

static void Attendance_OpenDeleteAllPage(void)
{
    memset(&g_app.card_manage, 0, sizeof(g_app.card_manage));
    g_app.card_manage.open = 1u;
    g_app.card_manage.mode = ATTENDANCE_CARD_PAGE_DELETE_ALL;
    g_app.card_manage.confirm = 1u;
    g_app.card_manage.physical_index = ATTENDANCE_INVALID_INDEX;
    MF_OpenCustomPage(Attendance_DeleteAllPage);
}

static void Attendance_OpenTimePageDirect(void)
{
    g_app.view = ATTENDANCE_VIEW_MENU;
    if (MF_GetCurrentMenu() == NULL)
    {
        MF_Start(g_app.menu_root);
    }

    memset(&g_app.time_edit, 0, sizeof(g_app.time_edit));
    g_app.time_edit.open = 1u;
    g_app.time_edit.field_index = 0u;
    if (g_app.rtc_valid != 0u)
    {
        g_app.time_edit.edit_time = g_app.current_time;
    }
    else
    {
        g_app.time_edit.edit_time.year = 26u;
        g_app.time_edit.edit_time.month = 1u;
        g_app.time_edit.edit_time.date = 1u;
        g_app.time_edit.edit_time.hour = 0u;
        g_app.time_edit.edit_time.minute = 0u;
        g_app.time_edit.edit_time.second = 0u;
    }
    g_app.time_edit.edit_time.day = Attendance_CalcWeekday(g_app.time_edit.edit_time.year,
                                                           g_app.time_edit.edit_time.month,
                                                           g_app.time_edit.edit_time.date);
    MF_OpenCustomPage(Attendance_TimePage);
    Key_ClearEvents();
}

static void Attendance_ClearRecordsAction(void)
{
    if ((g_app.storage_ready != 0u) && (Attendance_ClearAllRecords() == HAL_OK))
    {
        Attendance_SetTemporaryStatus(ATTENDANCE_STATUS_RECORDS_CLEARED,
                                      "RECORDS CLR",
                                      ATTENDANCE_STATUS_TIMEOUT_MS);
    }
    else
    {
        g_app.storage_ready = 0u;
        Attendance_SetTemporaryStatus(ATTENDANCE_STATUS_STORAGE_ERROR,
                                      "EEPROM ERR",
                                      ATTENDANCE_STATUS_TIMEOUT_MS);
    }
}

static void Attendance_RenderHome(void)
{
    char line[40];
    AttendanceRecord_t record;
    uint16_t display_index;

    OLED_Clear();
    memset(line, 0, sizeof(line));
    Attendance_FormatDateTime(&g_app.current_time, line, sizeof(line));
    OLED_ShowString(0, 0, line, OLED_6X8);

    if (g_app.has_latest_record != 0u)
    {
        (void)snprintf(line, sizeof(line), "Last:%s", g_app.latest_record.person_id);
        OLED_ShowString(0, 10, line, OLED_6X8);
        Attendance_FormatRecordTime(&g_app.latest_record, line, sizeof(line));
        OLED_ShowString(0, 20, line, OLED_6X8);
    }
    else
    {
        OLED_ShowString(0, 10, (char *)"Last:---", OLED_6X8);
        OLED_ShowString(0, 20, (char *)"No records", OLED_6X8);
    }

    (void)snprintf(line, sizeof(line), "Status:%s", Attendance_GetStatusText());
    OLED_ShowString(0, 30, line, OLED_6X8);

    (void)snprintf(line, sizeof(line), "Auth:%03u Rec:%03u",
                   (unsigned int)g_app.header.authorized_count,
                   (unsigned int)g_app.header.record_count);
    OLED_ShowString(0, 40, line, OLED_6X8);

    if (g_app.header.record_count > 0u)
    {
        display_index = g_app.rotate_offset;
        if (display_index >= g_app.header.record_count)
        {
            display_index = 0u;
        }
        if (Attendance_ReadRecordLogical(display_index, &record) == HAL_OK)
        {
            (void)snprintf(line, sizeof(line), "#%03u %s %02u:%02u:%02u",
                           (unsigned int)(display_index + 1u),
                           record.person_id,
                           (unsigned int)record.hour,
                           (unsigned int)record.minute,
                           (unsigned int)record.second);
            OLED_ShowString(0, 56, line, OLED_6X8);
        }
    }
    else
    {
        OLED_ShowString(0, 56, (char *)"ENTER rec  BACK menu", OLED_6X8);
    }

    OLED_Update();
}

static void Attendance_RenderRecords(void)
{
    char line[40];
    AttendanceRecord_t record;

    OLED_Clear();
    OLED_ShowString(0, 0, (char *)"Record View", OLED_6X8);

    if (g_app.header.record_count == 0u)
    {
        OLED_ShowString(0, 18, (char *)"No records", OLED_6X8);
        OLED_ShowString(0, 56, (char *)"ENTER home", OLED_6X8);
        OLED_Update();
        return;
    }

    Attendance_RefreshBrowseIndex();
    if (Attendance_ReadRecordLogical(g_app.browse_index, &record) != HAL_OK)
    {
        OLED_ShowString(0, 18, (char *)"Read failed", OLED_6X8);
        OLED_Update();
        return;
    }

    (void)snprintf(line, sizeof(line), "Rec:%03u/%03u",
                   (unsigned int)(g_app.browse_index + 1u),
                   (unsigned int)g_app.header.record_count);
    OLED_ShowString(0, 10, line, OLED_6X8);
    (void)snprintf(line, sizeof(line), "ID:%s", record.person_id);
    OLED_ShowString(0, 20, line, OLED_6X8);
    (void)snprintf(line, sizeof(line), "Date:20%02u/%02u/%02u",
                   (unsigned int)record.year,
                   (unsigned int)record.month,
                   (unsigned int)record.date);
    OLED_ShowString(0, 30, line, OLED_6X8);
    (void)snprintf(line, sizeof(line), "Time:%02u:%02u:%02u",
                   (unsigned int)record.hour,
                   (unsigned int)record.minute,
                   (unsigned int)record.second);
    OLED_ShowString(0, 40, line, OLED_6X8);
    OLED_ShowString(0, 56, (char *)"UP/DN browse ENT home", OLED_6X8);
    OLED_Update();
}

static void Attendance_RenderEnrollPage(void)
{
    char line[40];
    uint8_t cursor_x;

    OLED_ShowString(0, 0, (char *)"Enroll Card", OLED_6X8);
    if (g_app.enroll.waiting_card != 0u)
    {
        OLED_ShowString(0, 18, (char *)"Place card...", OLED_6X8);
        OLED_ShowString(0, 36, (char *)"BACK cancel", OLED_6X8);
        return;
    }

    if (g_app.enroll.existing_index >= 0)
    {
        OLED_ShowString(0, 10, (char *)"Update old card", OLED_6X8);
    }
    else
    {
        OLED_ShowString(0, 10, (char *)"New card", OLED_6X8);
    }

    (void)snprintf(line, sizeof(line), "ID:%s", g_app.enroll.person_id);
    OLED_ShowString(0, 24, line, OLED_6X8);
    cursor_x = (uint8_t)(18u + (g_app.enroll.cursor * 6u));
    OLED_ShowChar(cursor_x, 34, '^', OLED_6X8);
    OLED_ShowString(0, 48, (char *)"UP/DN digit ENT next", OLED_6X8);
    OLED_ShowString(0, 56, (char *)"BACK prev/cancel", OLED_6X8);
}

static void Attendance_RenderTimePage(void)
{
    char line[40];
    static const char *field_names[6] = {"YY", "MO", "DD", "HH", "MI", "SS"};

    OLED_ShowString(0, 0, (char *)"Set Time", OLED_6X8);
    (void)snprintf(line, sizeof(line), "20%02u/%02u/%02u",
                   (unsigned int)g_app.time_edit.edit_time.year,
                   (unsigned int)g_app.time_edit.edit_time.month,
                   (unsigned int)g_app.time_edit.edit_time.date);
    OLED_ShowString(0, 16, line, OLED_6X8);
    (void)snprintf(line, sizeof(line), "%02u:%02u:%02u W%u",
                   (unsigned int)g_app.time_edit.edit_time.hour,
                   (unsigned int)g_app.time_edit.edit_time.minute,
                   (unsigned int)g_app.time_edit.edit_time.second,
                   (unsigned int)g_app.time_edit.edit_time.day);
    OLED_ShowString(0, 28, line, OLED_6X8);
    (void)snprintf(line, sizeof(line), "Field:%s",
                   field_names[g_app.time_edit.field_index]);
    OLED_ShowString(0, 42, line, OLED_6X8);
    OLED_ShowString(0, 56, (char *)"UP/DN ENT save BACK", OLED_6X8);
}

static void Attendance_RenderInfoPage(void)
{
    char line[40];

    OLED_ShowString(0, 0, (char *)"System Info", OLED_6X8);
    (void)snprintf(line, sizeof(line), "Auth:%03u", (unsigned int)g_app.header.authorized_count);
    OLED_ShowString(0, 14, line, OLED_6X8);
    (void)snprintf(line, sizeof(line), "Records:%03u", (unsigned int)g_app.header.record_count);
    OLED_ShowString(0, 24, line, OLED_6X8);
    (void)snprintf(line, sizeof(line), "WiFi:%s", g_app.wifi_ready ? "OK" : "ERR");
    OLED_ShowString(0, 34, line, OLED_6X8);
    (void)snprintf(line, sizeof(line), "RTC:%s", g_app.rtc_valid ? "OK" : "SET");
    OLED_ShowString(0, 44, line, OLED_6X8);
    OLED_ShowString(0, 56, (char *)"BACK return", OLED_6X8);
}

static void Attendance_RenderCardManagePage(void)
{
    char line[40];
    char uid_line[24];
    uint16_t count;
    const AttendanceAuthorizedCard_t *entry;

    OLED_ShowString(0, 0,
                    (g_app.card_manage.mode == ATTENDANCE_CARD_PAGE_DELETE_ONE) ?
                        (char *)"Delete One" : (char *)"Card Library",
                    OLED_6X8);

    if (g_app.storage_ready == 0u)
    {
        OLED_ShowString(0, 20, (char *)"EEPROM ERR", OLED_6X8);
        OLED_ShowString(0, 56, (char *)"BACK return", OLED_6X8);
        return;
    }

    count = Attendance_CountAuthorizedCards();
    if (count == 0u)
    {
        OLED_ShowString(0, 20, (char *)"No cards", OLED_6X8);
        OLED_ShowString(0, 56, (char *)"BACK return", OLED_6X8);
        return;
    }

    Attendance_RefreshCardManageIndex();
    if (g_app.card_manage.physical_index >= ATTENDANCE_MAX_AUTH_CARDS)
    {
        OLED_ShowString(0, 20, (char *)"No cards", OLED_6X8);
        OLED_ShowString(0, 56, (char *)"BACK return", OLED_6X8);
        return;
    }

    entry = &g_app.authorized[g_app.card_manage.physical_index];
    (void)snprintf(line, sizeof(line), "Card:%03u/%03u",
                   (unsigned int)(g_app.card_manage.logical_index + 1u),
                   (unsigned int)count);
    OLED_ShowString(0, 10, line, OLED_6X8);
    (void)snprintf(line, sizeof(line), "ID:%s", entry->person_id);
    OLED_ShowString(0, 20, line, OLED_6X8);
    (void)snprintf(line, sizeof(line), "UIDL:%u Slot:%03u",
                   (unsigned int)entry->uid_len,
                   (unsigned int)g_app.card_manage.physical_index);
    OLED_ShowString(0, 30, line, OLED_6X8);
    Attendance_FormatUidSummary(entry, uid_line, sizeof(uid_line));
    OLED_ShowString(0, 40, uid_line, OLED_6X8);

    if ((g_app.card_manage.mode == ATTENDANCE_CARD_PAGE_DELETE_ONE) &&
        (g_app.card_manage.confirm != 0u))
    {
        OLED_ShowString(0, 48, (char *)"Delete this card?", OLED_6X8);
        OLED_ShowString(0, 56, (char *)"ENT yes BACK no", OLED_6X8);
    }
    else if (g_app.card_manage.mode == ATTENDANCE_CARD_PAGE_DELETE_ONE)
    {
        OLED_ShowString(0, 56, (char *)"UP/DN ENT del BACK", OLED_6X8);
    }
    else
    {
        OLED_ShowString(0, 56, (char *)"UP/DN browse BACK", OLED_6X8);
    }
}

static void Attendance_RenderDeleteAllPage(void)
{
    char line[40];
    uint16_t count;

    OLED_ShowString(0, 0, (char *)"Delete All Cards", OLED_6X8);

    if (g_app.storage_ready == 0u)
    {
        OLED_ShowString(0, 20, (char *)"EEPROM ERR", OLED_6X8);
        OLED_ShowString(0, 56, (char *)"BACK return", OLED_6X8);
        return;
    }

    count = Attendance_CountAuthorizedCards();
    if (count == 0u)
    {
        OLED_ShowString(0, 20, (char *)"No cards", OLED_6X8);
        OLED_ShowString(0, 56, (char *)"BACK return", OLED_6X8);
        return;
    }

    (void)snprintf(line, sizeof(line), "Auth:%03u", (unsigned int)count);
    OLED_ShowString(0, 16, line, OLED_6X8);
    OLED_ShowString(0, 30, (char *)"Keep records", OLED_6X8);
    OLED_ShowString(0, 44, (char *)"Clear card DB?", OLED_6X8);
    OLED_ShowString(0, 56, (char *)"ENT yes BACK no", OLED_6X8);
}

static void Attendance_EnrollPage(KeyEvent_t key, uint8_t *exit_flag)
{
    uint8_t result;

    if (g_app.enroll.open == 0u)
    {
        Attendance_OpenEnrollPage();
    }

    if (key == KEY_BACK)
    {
        if (g_app.enroll.waiting_card != 0u)
        {
            g_app.enroll.open = 0u;
            *exit_flag = 1u;
        }
        else if (g_app.enroll.cursor == 0u)
        {
            g_app.enroll.open = 0u;
            *exit_flag = 1u;
        }
        else
        {
            g_app.enroll.cursor--;
        }
    }
    else if ((g_app.enroll.waiting_card == 0u) && (key == KEY_UP))
    {
        if (g_app.enroll.person_id[g_app.enroll.cursor] >= '9')
        {
            g_app.enroll.person_id[g_app.enroll.cursor] = '0';
        }
        else
        {
            g_app.enroll.person_id[g_app.enroll.cursor]++;
        }
    }
    else if ((g_app.enroll.waiting_card == 0u) && (key == KEY_DOWN))
    {
        if (g_app.enroll.person_id[g_app.enroll.cursor] <= '0')
        {
            g_app.enroll.person_id[g_app.enroll.cursor] = '9';
        }
        else
        {
            g_app.enroll.person_id[g_app.enroll.cursor]--;
        }
    }
    else if ((g_app.enroll.waiting_card == 0u) && (key == KEY_ENTER))
    {
        if (g_app.enroll.cursor < (ATTENDANCE_PERSON_ID_LEN - 1u))
        {
            g_app.enroll.cursor++;
        }
        else
        {
            result = Attendance_AddOrUpdateAuthorizedCard(&g_app.enroll.card, g_app.enroll.person_id);
            if (result == 0u)
            {
                Attendance_SetTemporaryStatus(ATTENDANCE_STATUS_STORAGE_ERROR,
                                              "SAVE ERR",
                                              ATTENDANCE_STATUS_TIMEOUT_MS);
            }
            else if (result == 2u)
            {
                Attendance_SetTemporaryStatus(ATTENDANCE_STATUS_CARD_SAVED,
                                              "CARD UPDATED",
                                              ATTENDANCE_STATUS_TIMEOUT_MS);
            }
            else
            {
                Attendance_SetTemporaryStatus(ATTENDANCE_STATUS_CARD_SAVED,
                                              "CARD SAVED",
                                              ATTENDANCE_STATUS_TIMEOUT_MS);
            }
            g_app.enroll.open = 0u;
            *exit_flag = 1u;
        }
    }

    Attendance_RenderEnrollPage();
}

static void Attendance_TimePage(KeyEvent_t key, uint8_t *exit_flag)
{
    if (g_app.time_edit.open == 0u)
    {
        Attendance_OpenTimePageDirect();
    }

    if (key == KEY_UP)
    {
        Attendance_AdjustTimeField(1);
    }
    else if (key == KEY_DOWN)
    {
        Attendance_AdjustTimeField(-1);
    }
    else if (key == KEY_BACK)
    {
        if (g_app.time_edit.field_index == 0u)
        {
            g_app.time_edit.open = 0u;
            *exit_flag = 1u;
        }
        else
        {
            g_app.time_edit.field_index--;
        }
    }
    else if (key == KEY_ENTER)
    {
        if (g_app.time_edit.field_index < 5u)
        {
            g_app.time_edit.field_index++;
        }
        else
        {
            g_app.time_edit.edit_time.day = Attendance_CalcWeekday(g_app.time_edit.edit_time.year,
                                                                   g_app.time_edit.edit_time.month,
                                                                   g_app.time_edit.edit_time.date);
            DS1302_SetTime(&g_app.time_edit.edit_time);
            Attendance_UpdateCurrentTime();
            g_app.rtc_valid = 1u;
            Attendance_SetTemporaryStatus(ATTENDANCE_STATUS_SUCCESS,
                                          "TIME SAVED",
                                          ATTENDANCE_STATUS_TIMEOUT_MS);
            g_app.time_edit.open = 0u;
            *exit_flag = 1u;
        }
    }

    g_app.time_edit.edit_time.day = Attendance_CalcWeekday(g_app.time_edit.edit_time.year,
                                                           g_app.time_edit.edit_time.month,
                                                           g_app.time_edit.edit_time.date);
    Attendance_RenderTimePage();
}

static void Attendance_InfoPage(KeyEvent_t key, uint8_t *exit_flag)
{
    if (key == KEY_BACK)
    {
        *exit_flag = 1u;
    }

    Attendance_RenderInfoPage();
}

static void Attendance_CardLibraryPage(KeyEvent_t key, uint8_t *exit_flag)
{
    uint16_t count;

    if ((g_app.card_manage.open == 0u) ||
        (g_app.card_manage.mode != ATTENDANCE_CARD_PAGE_LIBRARY))
    {
        memset(&g_app.card_manage, 0, sizeof(g_app.card_manage));
        g_app.card_manage.open = 1u;
        g_app.card_manage.mode = ATTENDANCE_CARD_PAGE_LIBRARY;
        g_app.card_manage.physical_index = ATTENDANCE_INVALID_INDEX;
    }

    if (key == KEY_BACK)
    {
        g_app.card_manage.open = 0u;
        *exit_flag = 1u;
        return;
    }

    count = Attendance_CountAuthorizedCards();
    if (count > 0u)
    {
        if (key == KEY_DOWN)
        {
            g_app.card_manage.logical_index++;
            if (g_app.card_manage.logical_index >= count)
            {
                g_app.card_manage.logical_index = 0u;
            }
        }
        else if (key == KEY_UP)
        {
            if (g_app.card_manage.logical_index == 0u)
            {
                g_app.card_manage.logical_index = (uint16_t)(count - 1u);
            }
            else
            {
                g_app.card_manage.logical_index--;
            }
        }
    }

    Attendance_RenderCardManagePage();
}

static void Attendance_DeleteOnePage(KeyEvent_t key, uint8_t *exit_flag)
{
    uint16_t count;
    HAL_StatusTypeDef result;

    if ((g_app.card_manage.open == 0u) ||
        (g_app.card_manage.mode != ATTENDANCE_CARD_PAGE_DELETE_ONE))
    {
        memset(&g_app.card_manage, 0, sizeof(g_app.card_manage));
        g_app.card_manage.open = 1u;
        g_app.card_manage.mode = ATTENDANCE_CARD_PAGE_DELETE_ONE;
        g_app.card_manage.physical_index = ATTENDANCE_INVALID_INDEX;
    }

    count = Attendance_CountAuthorizedCards();
    if (key == KEY_BACK)
    {
        if (g_app.card_manage.confirm != 0u)
        {
            g_app.card_manage.confirm = 0u;
        }
        else
        {
            g_app.card_manage.open = 0u;
            *exit_flag = 1u;
            return;
        }
    }
    else if (count == 0u)
    {
        if (key == KEY_ENTER)
        {
            Attendance_SetTemporaryStatus(ATTENDANCE_STATUS_CARD_NOT_FOUND,
                                          "NO CARDS",
                                          ATTENDANCE_STATUS_TIMEOUT_MS);
        }
    }
    else if (g_app.card_manage.confirm != 0u)
    {
        if (key == KEY_ENTER)
        {
            Attendance_RefreshCardManageIndex();
            result = Attendance_DeleteAuthorizedEntry(g_app.card_manage.physical_index);
            if (result == HAL_OK)
            {
                Attendance_SetTemporaryStatus(ATTENDANCE_STATUS_CARD_DELETED,
                                              "CARD DEL",
                                              ATTENDANCE_STATUS_TIMEOUT_MS);
            }
            else
            {
                g_app.storage_ready = 0u;
                Attendance_SetTemporaryStatus(ATTENDANCE_STATUS_STORAGE_ERROR,
                                              "EEPROM ERR",
                                              ATTENDANCE_STATUS_TIMEOUT_MS);
            }
            g_app.card_manage.confirm = 0u;
            Attendance_RefreshCardManageIndex();
        }
    }
    else if (key == KEY_ENTER)
    {
        g_app.card_manage.confirm = 1u;
    }
    else if (key == KEY_DOWN)
    {
        g_app.card_manage.logical_index++;
        if (g_app.card_manage.logical_index >= count)
        {
            g_app.card_manage.logical_index = 0u;
        }
    }
    else if (key == KEY_UP)
    {
        if (g_app.card_manage.logical_index == 0u)
        {
            g_app.card_manage.logical_index = (uint16_t)(count - 1u);
        }
        else
        {
            g_app.card_manage.logical_index--;
        }
    }

    Attendance_RenderCardManagePage();
}

static void Attendance_DeleteAllPage(KeyEvent_t key, uint8_t *exit_flag)
{
    HAL_StatusTypeDef result;

    if ((g_app.card_manage.open == 0u) ||
        (g_app.card_manage.mode != ATTENDANCE_CARD_PAGE_DELETE_ALL))
    {
        memset(&g_app.card_manage, 0, sizeof(g_app.card_manage));
        g_app.card_manage.open = 1u;
        g_app.card_manage.mode = ATTENDANCE_CARD_PAGE_DELETE_ALL;
        g_app.card_manage.confirm = 1u;
        g_app.card_manage.physical_index = ATTENDANCE_INVALID_INDEX;
    }

    if (key == KEY_BACK)
    {
        g_app.card_manage.open = 0u;
        *exit_flag = 1u;
        return;
    }

    if (key == KEY_ENTER)
    {
        if (Attendance_CountAuthorizedCards() == 0u)
        {
            Attendance_SetTemporaryStatus(ATTENDANCE_STATUS_CARD_NOT_FOUND,
                                          "NO CARDS",
                                          ATTENDANCE_STATUS_TIMEOUT_MS);
        }
        else
        {
            result = Attendance_ClearAllAuthorizedCards();
            if (result == HAL_OK)
            {
                Attendance_SetTemporaryStatus(ATTENDANCE_STATUS_CARDS_CLEARED,
                                              "CARDS CLR",
                                              ATTENDANCE_STATUS_TIMEOUT_MS);
            }
            else
            {
                g_app.storage_ready = 0u;
                Attendance_SetTemporaryStatus(ATTENDANCE_STATUS_STORAGE_ERROR,
                                              "EEPROM ERR",
                                              ATTENDANCE_STATUS_TIMEOUT_MS);
            }
        }
    }

    Attendance_RenderDeleteAllPage();
}

static void Attendance_RefreshBrowseIndex(void)
{
    if (g_app.header.record_count == 0u)
    {
        g_app.browse_index = 0u;
        g_app.rotate_offset = 0u;
        return;
    }

    if (g_app.browse_index >= g_app.header.record_count)
    {
        g_app.browse_index = (uint16_t)(g_app.header.record_count - 1u);
    }

    if (g_app.rotate_offset >= g_app.header.record_count)
    {
        g_app.rotate_offset = 0u;
    }
}

static void Attendance_RefreshCardManageIndex(void)
{
    uint16_t count;

    count = Attendance_CountAuthorizedCards();
    g_app.header.authorized_count = count;

    if (count == 0u)
    {
        g_app.card_manage.logical_index = 0u;
        g_app.card_manage.physical_index = ATTENDANCE_INVALID_INDEX;
        return;
    }

    if (g_app.card_manage.logical_index >= count)
    {
        g_app.card_manage.logical_index = (uint16_t)(count - 1u);
    }

    g_app.card_manage.physical_index =
        Attendance_GetAuthorizedPhysicalIndex(g_app.card_manage.logical_index);

    if (g_app.card_manage.physical_index == ATTENDANCE_INVALID_INDEX)
    {
        g_app.card_manage.logical_index = 0u;
        g_app.card_manage.physical_index = Attendance_GetAuthorizedPhysicalIndex(0u);
    }
}

static void Attendance_FormatUidSummary(const AttendanceAuthorizedCard_t *entry, char *buf, uint16_t len)
{
    uint16_t used;
    uint8_t index;
    uint8_t byte_count;

    if ((entry == NULL) || (buf == NULL) || (len == 0u))
    {
        return;
    }

    buf[0] = '\0';
    used = 0u;
    byte_count = entry->uid_len;
    if (byte_count > 4u)
    {
        byte_count = 4u;
    }

    Attendance_AppendFormat(buf, &used, len, "UID:");
    for (index = 0u; index < byte_count; index++)
    {
        Attendance_AppendFormat(buf, &used, len, "%02X", (unsigned int)entry->uid[index]);
    }
    if (entry->uid_len > byte_count)
    {
        Attendance_AppendFormat(buf, &used, len, "..");
    }
}

static void Attendance_RotateHomeRecord(void)
{
    if (g_app.header.record_count == 0u)
    {
        g_app.rotate_offset = 0u;
        return;
    }

    g_app.rotate_offset++;
    if (g_app.rotate_offset >= g_app.header.record_count)
    {
        g_app.rotate_offset = 0u;
    }
}

static void Attendance_ProcessInput(void)
{
    uint8_t mask;
    KeyEvent_t key;

    mask = Key_GetStableMask();

    if (g_app.view != ATTENDANCE_VIEW_MENU)
    {
        if (((mask & (KEY_MASK_UP | KEY_MASK_BACK)) == (KEY_MASK_UP | KEY_MASK_BACK)) ||
            ((mask & (KEY_MASK_DOWN | KEY_MASK_BACK)) == (KEY_MASK_DOWN | KEY_MASK_BACK)))
        {
            if (g_app.combo_latched == 0u)
            {
                if ((mask & (KEY_MASK_UP | KEY_MASK_BACK)) == (KEY_MASK_UP | KEY_MASK_BACK))
                {
                    Attendance_ClearRecordsAction();
                }
                else
                {
                    Attendance_OpenTimePageDirect();
                }
                g_app.combo_latched = 1u;
                Key_ClearEvents();
            }
            return;
        }
    }

    if ((g_app.combo_latched != 0u) && ((mask & (KEY_MASK_UP | KEY_MASK_DOWN | KEY_MASK_BACK)) == 0u))
    {
        g_app.combo_latched = 0u;
    }

    key = Key_Scan();
    if (key == KEY_NONE)
    {
        return;
    }

    if (g_app.view == ATTENDANCE_VIEW_MENU)
    {
        if ((key == KEY_BACK) && (MF_CanExit() != 0u))
        {
            Attendance_LeaveMenu();
        }
        else
        {
            MF_Process(key);
        }
        return;
    }

    if (key == KEY_BACK)
    {
        Attendance_EnterMenu();
        return;
    }

    if (key == KEY_ENTER)
    {
        if (g_app.view == ATTENDANCE_VIEW_HOME)
        {
            g_app.view = ATTENDANCE_VIEW_RECORDS;
        }
        else
        {
            g_app.view = ATTENDANCE_VIEW_HOME;
        }
        return;
    }

    if ((key == KEY_DOWN) && (g_app.header.record_count > 0u))
    {
        if (g_app.view == ATTENDANCE_VIEW_RECORDS)
        {
            if (g_app.browse_index < (g_app.header.record_count - 1u))
            {
                g_app.browse_index++;
            }
        }
        else
        {
            Attendance_RotateHomeRecord();
        }
        return;
    }

    if ((key == KEY_UP) && (g_app.header.record_count > 0u))
    {
        if (g_app.view == ATTENDANCE_VIEW_RECORDS)
        {
            if (g_app.browse_index > 0u)
            {
                g_app.browse_index--;
            }
        }
        else
        {
            if (g_app.rotate_offset == 0u)
            {
                g_app.rotate_offset = (uint16_t)(g_app.header.record_count - 1u);
            }
            else
            {
                g_app.rotate_offset--;
            }
        }
    }
}

static void Attendance_TcpCallback(ESP_DataPacket_t *packet)
{
    char command[80];
    uint16_t copy_len;

    if (packet == NULL)
    {
        return;
    }

    if (packet->link_id >= ATTENDANCE_TCP_MAX_LINKS)
    {
        return;
    }

    Attendance_MarkTcpLink(packet->link_id);
    memset(command, 0, sizeof(command));
    copy_len = packet->len;
    if (copy_len >= sizeof(command))
    {
        copy_len = (uint16_t)(sizeof(command) - 1u);
    }
    memcpy(command, packet->data, copy_len);
    Attendance_TrimTcpCommand(command);

    if (strcmp(command, "HELLO") == 0)
    {
        (void)Attendance_SendTcpLine(packet->link_id, "OK ATTENDANCE TCP V1");
    }
    else if (strcmp(command, "PING") == 0)
    {
        (void)Attendance_SendTcpLine(packet->link_id, "PONG");
    }
    else if (strcmp(command, "GET STATUS") == 0)
    {
        Attendance_SendTcpStatus(packet->link_id);
    }
    else if (strcmp(command, "GET LATEST") == 0)
    {
        Attendance_SendTcpLatest(packet->link_id);
    }
    else if ((strncmp(command, "GET RECORDS", 11u) == 0) &&
             ((command[11] == '\0') || (command[11] == ' ') || (command[11] == '\t')))
    {
        Attendance_SendTcpRecords(packet->link_id, Attendance_ParseRecordLimit(command));
    }
    else
    {
        (void)Attendance_SendTcpLine(packet->link_id, "ERR BAD_CMD");
    }
}

static void Attendance_AppendFormat(char *buf, uint16_t *used, uint16_t size, const char *fmt, ...)
{
    va_list args;
    int written;
    uint16_t remain;

    if ((buf == NULL) || (used == NULL) || (fmt == NULL) || (*used >= size))
    {
        return;
    }

    remain = (uint16_t)(size - *used);
    va_start(args, fmt);
    written = vsnprintf(&buf[*used], remain, fmt, args);
    va_end(args);

    if (written < 0)
    {
        return;
    }

    if ((uint16_t)written >= remain)
    {
        *used = (uint16_t)(size - 1u);
    }
    else
    {
        *used = (uint16_t)(*used + (uint16_t)written);
    }
}

static const char *Attendance_GetProtocolStatusText(void)
{
    uint32_t now;

    if (g_app.storage_ready == 0u)
    {
        return "EEPROM_ERR";
    }
    if (g_app.rtc_valid == 0u)
    {
        return "SET_TIME";
    }
    if (g_app.wifi_ready == 0u)
    {
        return "WIFI_ERR";
    }

    now = HAL_GetTick();
    if ((g_app.temp_status[0] != '\0') && ((int32_t)(g_app.temp_status_deadline - now) > 0))
    {
        switch (g_app.status)
        {
        case ATTENDANCE_STATUS_SUCCESS:
            return "SIGN_OK";
        case ATTENDANCE_STATUS_INVALID_CARD:
            return "INVALID_CARD";
        case ATTENDANCE_STATUS_TIME_INVALID:
            return "SET_TIME";
        case ATTENDANCE_STATUS_STORAGE_ERROR:
            return "EEPROM_ERR";
        case ATTENDANCE_STATUS_WIFI_ERROR:
            return "WIFI_ERR";
        case ATTENDANCE_STATUS_RECORDS_CLEARED:
            return "RECORDS_CLEARED";
        case ATTENDANCE_STATUS_CARD_SAVED:
            return "CARD_SAVED";
        case ATTENDANCE_STATUS_CARD_DELETED:
            return "CARD_DELETED";
        case ATTENDANCE_STATUS_CARD_NOT_FOUND:
            return "CARD_NOT_FOUND";
        case ATTENDANCE_STATUS_CARDS_CLEARED:
            return "CARDS_CLEARED";
        default:
            break;
        }
    }

    return "WAIT_CARD";
}

static void Attendance_FormatDateTimeTcp(const DS1302_Time_t *time, char *buf, uint16_t len)
{
    if ((time == NULL) || (buf == NULL) || (len == 0u))
    {
        return;
    }

    (void)snprintf(buf, len, "20%02u-%02u-%02u %02u:%02u:%02u",
                   (unsigned int)time->year,
                   (unsigned int)time->month,
                   (unsigned int)time->date,
                   (unsigned int)time->hour,
                   (unsigned int)time->minute,
                   (unsigned int)time->second);
}

static void Attendance_FormatRecordTimeTcp(const AttendanceRecord_t *record, char *buf, uint16_t len)
{
    if ((record == NULL) || (buf == NULL) || (len == 0u))
    {
        return;
    }

    (void)snprintf(buf, len, "20%02u-%02u-%02u %02u:%02u:%02u",
                   (unsigned int)record->year,
                   (unsigned int)record->month,
                   (unsigned int)record->date,
                   (unsigned int)record->hour,
                   (unsigned int)record->minute,
                   (unsigned int)record->second);
}

static void Attendance_TrimTcpCommand(char *cmd)
{
    uint16_t read_index;
    uint16_t write_index;

    if (cmd == NULL)
    {
        return;
    }

    read_index = 0u;
    while ((cmd[read_index] == ' ') || (cmd[read_index] == '\t'))
    {
        read_index++;
    }

    write_index = 0u;
    while ((cmd[read_index] != '\0') && (cmd[read_index] != '\r') && (cmd[read_index] != '\n'))
    {
        char ch = cmd[read_index++];
        if ((ch >= 'a') && (ch <= 'z'))
        {
            ch = (char)(ch - ('a' - 'A'));
        }
        cmd[write_index++] = ch;
    }

    while ((write_index > 0u) &&
           ((cmd[write_index - 1u] == ' ') || (cmd[write_index - 1u] == '\t')))
    {
        write_index--;
    }
    cmd[write_index] = '\0';
}

static uint16_t Attendance_ParseRecordLimit(const char *cmd)
{
    uint16_t value;
    uint8_t has_digit;

    if (cmd == NULL)
    {
        return 8u;
    }

    cmd += 11u;
    while ((*cmd == ' ') || (*cmd == '\t'))
    {
        cmd++;
    }

    value = 0u;
    has_digit = 0u;
    while ((*cmd >= '0') && (*cmd <= '9'))
    {
        has_digit = 1u;
        value = (uint16_t)((value * 10u) + (uint16_t)(*cmd - '0'));
        if (value > ATTENDANCE_TCP_RECORDS_MAX)
        {
            value = ATTENDANCE_TCP_RECORDS_MAX;
            break;
        }
        cmd++;
    }

    if (has_digit == 0u)
    {
        value = 8u;
    }
    if (value > ATTENDANCE_TCP_RECORDS_MAX)
    {
        value = ATTENDANCE_TCP_RECORDS_MAX;
    }

    return value;
}

static void Attendance_MarkTcpLink(uint8_t link_id)
{
    if (link_id < ATTENDANCE_TCP_MAX_LINKS)
    {
        g_app.tcp_link_mask |= (uint8_t)(1u << link_id);
    }
}

static void Attendance_ClearTcpLink(uint8_t link_id)
{
    if (link_id < ATTENDANCE_TCP_MAX_LINKS)
    {
        g_app.tcp_link_mask &= (uint8_t)(~(uint8_t)(1u << link_id));
    }
}

static uint8_t Attendance_SendTcpText(uint8_t link_id, const char *text)
{
    uint16_t len;

    if ((text == NULL) || (link_id >= ATTENDANCE_TCP_MAX_LINKS))
    {
        return 0u;
    }

    len = (uint16_t)strlen(text);
    if ((len == 0u) || (len > ESP_TX_BUF_SIZE))
    {
        return 0u;
    }

    if (ESP_SendData(link_id, (const uint8_t *)text, len) != ESP_OK)
    {
        Attendance_ClearTcpLink(link_id);
        (void)ESP_CloseLink(link_id);
        return 0u;
    }

    Attendance_MarkTcpLink(link_id);
    return 1u;
}

static uint8_t Attendance_SendTcpLine(uint8_t link_id, const char *fmt, ...)
{
    va_list args;
    int written;
    uint16_t used;

    if (fmt == NULL)
    {
        return 0u;
    }

    memset(g_app.protocol_buffer, 0, sizeof(g_app.protocol_buffer));
    va_start(args, fmt);
    written = vsnprintf(g_app.protocol_buffer, sizeof(g_app.protocol_buffer), fmt, args);
    va_end(args);

    if ((written < 0) || ((uint16_t)written >= (sizeof(g_app.protocol_buffer) - 3u)))
    {
        return 0u;
    }

    used = (uint16_t)written;
    g_app.protocol_buffer[used++] = '\r';
    g_app.protocol_buffer[used++] = '\n';
    g_app.protocol_buffer[used] = '\0';

    return Attendance_SendTcpText(link_id, g_app.protocol_buffer);
}

static void Attendance_SendTcpStatus(uint8_t link_id)
{
    char time_buf[40];

    Attendance_FormatDateTimeTcp(&g_app.current_time, time_buf, sizeof(time_buf));
    (void)Attendance_SendTcpLine(link_id,
                                 "STATUS time=%s state=%s auth=%u records=%u",
                                 time_buf,
                                 Attendance_GetProtocolStatusText(),
                                 (unsigned int)g_app.header.authorized_count,
                                 (unsigned int)g_app.header.record_count);
}

static void Attendance_SendTcpLatest(uint8_t link_id)
{
    char time_buf[40];

    if (g_app.has_latest_record == 0u)
    {
        (void)Attendance_SendTcpLine(link_id, "LATEST none");
        return;
    }

    Attendance_FormatRecordTimeTcp(&g_app.latest_record, time_buf, sizeof(time_buf));
    (void)Attendance_SendTcpLine(link_id,
                                 "LATEST id=%s time=%s",
                                 g_app.latest_record.person_id,
                                 time_buf);
}

static void Attendance_SendTcpRecords(uint8_t link_id, uint16_t requested_count)
{
    AttendanceRecord_t record;
    uint16_t limit;
    uint16_t index;
    uint16_t emitted;
    uint16_t used;
    char time_buf[40];

    limit = requested_count;
    if (limit > ATTENDANCE_TCP_RECORDS_MAX)
    {
        limit = ATTENDANCE_TCP_RECORDS_MAX;
    }
    if (limit > g_app.header.record_count)
    {
        limit = g_app.header.record_count;
    }

    emitted = 0u;
    for (index = 0u; index < limit; index++)
    {
        if (Attendance_ReadRecordLogical(index, &record) == HAL_OK)
        {
            emitted++;
        }
    }

    memset(g_app.protocol_buffer, 0, sizeof(g_app.protocol_buffer));
    used = 0u;
    Attendance_AppendFormat(g_app.protocol_buffer, &used, sizeof(g_app.protocol_buffer),
                            "RECORDS count=%u\r\n",
                            (unsigned int)emitted);

    for (index = 0u; index < limit; index++)
    {
        if (Attendance_ReadRecordLogical(index, &record) != HAL_OK)
        {
            continue;
        }

        Attendance_FormatRecordTimeTcp(&record, time_buf, sizeof(time_buf));
        Attendance_AppendFormat(g_app.protocol_buffer, &used, sizeof(g_app.protocol_buffer),
                                "REC id=%s time=%s\r\n",
                                record.person_id,
                                time_buf);
    }

    Attendance_AppendFormat(g_app.protocol_buffer, &used, sizeof(g_app.protocol_buffer), "END\r\n");
    (void)Attendance_SendTcpText(link_id, g_app.protocol_buffer);
}

static void Attendance_PushSignOkEvent(const AttendanceRecord_t *record)
{
    uint8_t link_id;
    uint8_t link_mask;
    char time_buf[40];

    link_mask = (uint8_t)(g_app.tcp_link_mask | ESP_GetActiveLinkMask());
    if ((record == NULL) || (link_mask == 0u))
    {
        return;
    }

    Attendance_FormatRecordTimeTcp(record, time_buf, sizeof(time_buf));
    for (link_id = 0u; link_id < ATTENDANCE_TCP_MAX_LINKS; link_id++)
    {
        if ((link_mask & (uint8_t)(1u << link_id)) != 0u)
        {
            (void)Attendance_SendTcpLine(link_id,
                                         "EVENT SIGN_OK id=%s time=%s",
                                         record->person_id,
                                         time_buf);
        }
    }
}

void Attendance_AppInit(void)
{
    uint32_t now;

    memset(&g_app, 0, sizeof(g_app));
    g_app.header.last_index = ATTENDANCE_INVALID_INDEX;
    g_app.view = ATTENDANCE_VIEW_HOME;
    g_app.status = ATTENDANCE_STATUS_WAIT_CARD;

    OLED_Init();
    Key_Init();
    Buzzer_Init();
    Attendance_LED_Init();
    AT24C256_Init();
    DS1302_Init();
    RC522_Init();

    Attendance_BuildMenu();
    if (Attendance_LoadStorage() != HAL_OK)
    {
        g_app.storage_ready = 0u;
        Attendance_SetTemporaryStatus(ATTENDANCE_STATUS_STORAGE_ERROR,
                                      "EEPROM ERR",
                                      ATTENDANCE_STATUS_TIMEOUT_MS);
    }

    Attendance_UpdateCurrentTime();

    if (ESP_Init() == ESP_OK)
    {
        g_app.wifi_ready = 1u;
    }
    else
    {
        char wifi_status[16];

        g_app.wifi_ready = 0u;
        (void)snprintf(wifi_status, sizeof(wifi_status), "WIFI E%02u", (unsigned int)ESP_GetLastErrorStep());
        Attendance_SetTemporaryStatus(ATTENDANCE_STATUS_WIFI_ERROR,
                                      wifi_status,
                                      ATTENDANCE_STATUS_TIMEOUT_MS);
    }
    ESP_RegisterCallback(Attendance_TcpCallback);

    now = HAL_GetTick();
    g_app.next_poll_tick = now;
    g_app.next_time_tick = now;
    g_app.next_display_tick = now;
    g_app.next_rotate_tick = now + ATTENDANCE_ROTATE_MS;

    OLED_Clear();
    OLED_Update();
}

void Attendance_AppLoop(void)
{
    uint32_t now;

    now = HAL_GetTick();

    if ((int32_t)(now - g_app.next_time_tick) >= 0)
    {
        Attendance_UpdateCurrentTime();
        g_app.next_time_tick = now + ATTENDANCE_TIME_REFRESH_MS;
    }

    Attendance_UpdateIndicators();
    ESP_Process();
    Attendance_ProcessInput();

    if (g_app.view == ATTENDANCE_VIEW_MENU)
    {
        Attendance_PollEnrollCard();
    }
    else
    {
        Attendance_PollAttendanceCard();
        if ((g_app.view == ATTENDANCE_VIEW_HOME) && ((int32_t)(now - g_app.next_rotate_tick) >= 0))
        {
            Attendance_RotateHomeRecord();
            g_app.next_rotate_tick = now + ATTENDANCE_ROTATE_MS;
        }
    }

    if ((int32_t)(now - g_app.next_display_tick) >= 0)
    {
        if (g_app.view == ATTENDANCE_VIEW_MENU)
        {
            MF_Render();
        }
        else if (g_app.view == ATTENDANCE_VIEW_RECORDS)
        {
            Attendance_RenderRecords();
        }
        else
        {
            Attendance_RenderHome();
        }

        g_app.next_display_tick = now + ATTENDANCE_DISPLAY_REFRESH_MS;
    }
}
