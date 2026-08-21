#ifndef __DHT11_H
#define __DHT11_H

#include "stm32f1xx_hal.h"
#include ".\SYSTEM\delay\delay.h"

#define DHT11_Data_PORT GPIOA
#define DHT11_Data_PIN GPIO_PIN_5

#define DHT11_STATUS_OK 0U
#define DHT11_STATUS_NO_RESPONSE 1U
#define DHT11_STATUS_CHECKSUM_ERROR 2U
#define DHT11_STATUS_TIMEOUT 3U
#define DHT11_STATUS_RANGE_ERROR 4U

typedef struct
{
    uint8_t humi_int;
    uint8_t temp_int;
} DHT11_Data_t;

extern DHT11_Data_t DHT11_Data;

uint8_t DHT11_Init(void);
uint8_t DHT11_ReadData(void);
uint8_t DHT11_GetTemperature(void);
uint8_t DHT11_GetHumidity(void);
uint8_t DHT11_Read(uint8_t *humi_int, uint8_t *humi_dec, uint8_t *temp_int, uint8_t *temp_dec);

#endif /* __DHT11_H */
