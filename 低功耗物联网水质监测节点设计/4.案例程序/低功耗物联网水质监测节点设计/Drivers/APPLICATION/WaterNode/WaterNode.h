#ifndef __WATER_NODE_H
#define __WATER_NODE_H

#include "stm32f1xx_hal.h"

#define WATER_PROTOCOL_JSON 0
#define WATER_PROTOCOL_CN 1

#ifndef WATER_PROTOCOL
#define WATER_PROTOCOL 0
#endif

#ifndef WATER_DEBUG_MODE
/* 0: normal, 1: debug */
#define WATER_DEBUG_MODE 0
#endif

void WaterNode_Init(void);
void WaterNode_Loop(void);

#endif
