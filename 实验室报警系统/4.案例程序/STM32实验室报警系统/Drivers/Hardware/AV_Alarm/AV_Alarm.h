#ifndef __AV_ALARM_H
#define __AV_ALARM_H

#include "stm32f1xx_hal.h"

#define ALARM_GPIO_PORT GPIOB
#define ALARM_GPIO_PIN GPIO_PIN_1

void AV_Alarm_Init(void);
void AV_Alarm_On(void);
void AV_Alarm_Off(void);
uint8_t Get_AV_Alarm_State(void);

#endif /* __AV_ALARM_H */
