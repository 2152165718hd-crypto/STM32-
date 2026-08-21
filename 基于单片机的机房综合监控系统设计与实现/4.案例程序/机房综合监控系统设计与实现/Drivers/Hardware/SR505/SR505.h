#ifndef __SR505_H__
#define __SR505_H__

#include "stm32f1xx_hal.h"

#define SR505_GPIO_PORT GPIOA
#define SR505_GPIO_PIN GPIO_PIN_6

typedef enum
{
	SR505_NO_MOTION = 0,
	SR505_MOTION_ACTIVE = 1
} SR505_State_t;

void SR505_Init(void);
uint8_t SR505_GetLevel(void);
SR505_State_t SR505_GetState(void);
uint8_t SR505_IsMotionDetected(void);

void SR505_Update(void);
uint8_t SR505_GetMotionStartEvent(void);
uint8_t SR505_GetMotionStopEvent(void);


#endif /* __SR505_H__ */
