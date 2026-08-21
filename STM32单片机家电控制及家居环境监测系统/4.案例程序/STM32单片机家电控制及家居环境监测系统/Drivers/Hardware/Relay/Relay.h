#ifndef __RELAY_H__
#define __RELAY_H__

#include "stm32f1xx_hal.h"

#define ACTIVE_LEVEL  1

#define RELAY_GPIO_PORT GPIOB
#define RELAY_AC_PIN GPIO_PIN_11                        //空调控制引脚
#define RELAY_RC_PIN GPIO_PIN_10                         //电饭煲控制引脚

void Relay_Init(void);
void Relay_SetState(uint8_t relay_id, uint8_t state);

#endif
