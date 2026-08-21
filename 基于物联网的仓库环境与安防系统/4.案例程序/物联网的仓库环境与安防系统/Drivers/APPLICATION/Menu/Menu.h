#ifndef __MENU_H
#define __MENU_H

#include "stm32f1xx_hal.h"
#include ".\APPLICATION\Menu\MenuFramework.h"

void Menu_SystemInit(void);
void Menu_SystemLoop(void);
void Menu_SetRemoteAlarm(uint8_t alarm_on);

#endif /* __MENU_H */
