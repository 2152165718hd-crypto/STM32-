#ifndef _ONENET_CONFIG_H_
#define _ONENET_CONFIG_H_

/* Synced from root config.ini / Token.log for product Vk99hTUKo2. */
#define ONENET_PRODUCT_ID "Vk99hTUKo2"
#define ONENET_DEVICE_NAME "don1ng666"
#define ONENET_TOKEN "version=2018-10-31&res=products%2FVk99hTUKo2%2Fdevices%2Fdon1ng666&et=1861891200&method=md5&sign=xHiy1gRiyCGTbE1lGcJ%2BTg%3D%3D"

#define ONENET_MQTT_HOST "mqtts.heclouds.com"
#define ONENET_MQTT_PORT 1883U

/*
 * Root cloud files do not include Wi-Fi credentials. These defaults preserve
 * the existing project test values; replace them with the deployment router
 * credentials before commercial installation.
 */
#ifndef ONENET_WIFI_SSID
#define ONENET_WIFI_SSID "don1ng"
#endif

#ifndef ONENET_WIFI_PASS
#define ONENET_WIFI_PASS "88888888"
#endif

#define ONENET_FIRMWARE_VERSION "locker-cloud-1.0.0"
#define ONENET_PROTOCOL_VERSION 1

#endif
