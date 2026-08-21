#ifndef __RELAY_H__
#define __RELAY_H__

#include "stm32f4xx_hal.h"
#include <stdint.h>

#define RELAY_COUNT 16U

#ifndef RELAY_ACTIVE_LEVEL
#define RELAY_ACTIVE_LEVEL GPIO_PIN_SET
#endif

void Relay_Init(void);
void Relay_SetState(uint8_t relay_id, uint8_t state);
void Relay_AllOff(void);

#endif
