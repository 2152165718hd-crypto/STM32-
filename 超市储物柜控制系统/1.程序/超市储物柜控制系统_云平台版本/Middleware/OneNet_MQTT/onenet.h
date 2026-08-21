#ifndef _ONENET_H_
#define _ONENET_H_

#include <stdint.h>

#define ONENET_LOCKER_COUNT 16U

typedef struct
{
    uint8_t op;
    uint8_t locker_id;
    int16_t face_id;
    uint8_t result;
    char timestamp[20];
} OneNetLockerRecord_t;

typedef struct
{
    uint8_t protocol_version;
    const char *firmware_version;
    const char *device_time;
    uint8_t device_state;
    uint8_t cloud_connected;
    int32_t wifi_rssi;
    uint8_t locker_count;
    uint8_t used_count;
    uint8_t free_count;
    uint16_t locker_bitmap;
    uint8_t locker_occupancy[ONENET_LOCKER_COUNT];
    uint8_t active_locker_id;
    uint8_t door_open;
    uint32_t door_open_elapsed_sec;
    uint8_t alarm_status;
    uint8_t buzzer_active;
    uint8_t current_operation;
    uint16_t record_count;
    OneNetLockerRecord_t last_record;
    uint8_t remote_control_enabled;
    uint16_t door_timeout_sec;
    uint16_t status_report_interval_sec;
    uint8_t maintenance_mode;
    const char *last_error_message;
} OneNetLockerStatus_t;

typedef struct
{
    uint8_t has_door_timeout_sec;
    uint16_t door_timeout_sec;
    uint8_t has_status_report_interval_sec;
    uint16_t status_report_interval_sec;
    uint8_t has_remote_control_enabled;
    uint8_t remote_control_enabled;
    uint8_t has_maintenance_mode;
    uint8_t maintenance_mode;
} OneNetPropertySet_t;

typedef enum
{
    ONENET_SERVICE_UNKNOWN = 0,
    ONENET_SERVICE_QUERY_STATUS = 1,
    ONENET_SERVICE_OPEN_LOCKER = 2,
    ONENET_SERVICE_CLEAR_ALARM = 3,
    ONENET_SERVICE_EXPORT_RECORDS = 4,
    ONENET_SERVICE_CLEAR_ALL_LOCKERS = 5,
    ONENET_SERVICE_SET_DEVICE_TIME = 6,
    ONENET_SERVICE_REBOOT_DEVICE = 7
} OneNetServiceType_t;

typedef struct
{
    OneNetServiceType_t type;
    char identifier[32];
    char request_id[32];
    int32_t locker_id;
    int32_t reason;
    int32_t limit;
    char operator_id[64];
    char confirm_code[32];
    char time[32];
} OneNetServiceRequest_t;

typedef void (*OneNet_PropertySetCallback_t)(const OneNetPropertySet_t *set, const char *request_id);
typedef void (*OneNet_ServiceCallback_t)(const OneNetServiceRequest_t *request);

uint8_t OneNet_Init(void);
void OneNet_RegisterPropertySetCallback(OneNet_PropertySetCallback_t cb);
void OneNet_RegisterServiceCallback(OneNet_ServiceCallback_t cb);
void OneNet_Process(void);
uint8_t OneNet_IsConnected(void);
uint8_t OneNet_IsReady(void);

uint8_t OneNet_PublishProperties(const OneNetLockerStatus_t *status);
uint8_t OneNet_PublishEventValue(const char *event_id, const char *value_json);
uint8_t OneNet_ReplyService(const char *service_id,
                            const char *request_id,
                            uint16_t platform_code,
                            const char *message,
                            const char *data_json);

void OneNet_RequestImmediateReport(void);
uint8_t OneNet_ConsumeImmediateReportFlag(void);
const char *OneNet_ServiceName(OneNetServiceType_t type);

#endif
