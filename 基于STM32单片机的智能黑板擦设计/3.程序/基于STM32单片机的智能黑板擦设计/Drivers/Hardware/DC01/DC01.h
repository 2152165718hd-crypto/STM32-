#ifndef __DC01_H
#define __DC01_H

#include <stdint.h>

typedef struct
{
	uint16_t pm25_x10;
	uint16_t pm10_x10;
	uint8_t id_low;
	uint8_t id_high;
} DC01_Data_t;

void DC01_Init(uint32_t baudrate);

uint8_t DC01_HasNewData(void);
uint8_t DC01_ReadDataRaw(DC01_Data_t *data);
uint8_t DC01_ReadPMugm3(uint16_t *pm25, uint16_t *pm10);

#endif
