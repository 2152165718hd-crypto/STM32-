#ifndef _ONENET_CONFIG_H_
#define _ONENET_CONFIG_H_

#define ONENET_PRODUCT_ID "VXTy1nU2xM"
#define ONENET_DEVICE_NAME "don1ng"
#define ONENET_TOKEN "version=2018-10-31&res=products%2FVXTy1nU2xM%2Fdevices%2Fdon1ng&et=1861891200&method=md5&sign=t3TUGdZ3M0Flh%2FI7Xt0OnA%3D%3D"

#define ONENET_MQTT_HOST "studio-mqtt.heclouds.com"
#define ONENET_MQTT_FALLBACK_HOST "mqtts.heclouds.com"
#define ONENET_MQTT_PORT 1883U
#define ONENET_KEEPALIVE_SECONDS 60U
#define ONENET_MQTT_CLEAN_SESSION 1U

#define ONENET_PROPERTY_POST_TOPIC_TEMPLATE "$sys/%s/%s/thing/property/post"
#define ONENET_PROPERTY_POST_REPLY_TOPIC_TEMPLATE "$sys/%s/%s/thing/property/post/reply"
#define ONENET_PROPERTY_SET_TOPIC_TEMPLATE "$sys/%s/%s/thing/property/set"
#define ONENET_PROPERTY_SET_REPLY_TOPIC_TEMPLATE "$sys/%s/%s/thing/property/set_reply"
#define ONENET_PROPERTY_POST_QOS 0U
#define ONENET_PROPERTY_SET_QOS 0U

#define ONENET_CONNECT_RETRY_MS 5000U
#define ONENET_STATUS_POLL_MS 30000U

#define ONENET_TOPIC_BUF_SIZE 160U
#define ONENET_PAYLOAD_BUF_SIZE 512U

#define ONENET_PROPERTY_COUNT 8U
#define ONENET_PROPERTY_ALARM_STATUS "alarm_status"
#define ONENET_PROPERTY_ILLUMINANCE "illuminance"
#define ONENET_PROPERTY_LIGHT_ONLINE "light_online"
#define ONENET_PROPERTY_REPORT_PERIOD "report_period"
#define ONENET_PROPERTY_SLAVE_ONLINE "slave_online"
#define ONENET_PROPERTY_TEMPERATURE "temperature"
#define ONENET_PROPERTY_TEMPERATURE_THRESHOLD "temperature_threshold"
#define ONENET_PROPERTY_UPTIME "uptime"

#endif /* _ONENET_CONFIG_H_ */
