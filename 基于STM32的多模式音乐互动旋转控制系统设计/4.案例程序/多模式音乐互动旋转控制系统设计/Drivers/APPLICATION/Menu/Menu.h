#ifndef __MENU_H
#define __MENU_H

#include <stdint.h>

#include "stm32f1xx_hal.h"

typedef enum
{
    MENU_APP_MODE_MUSIC = 0,
    MENU_APP_MODE_MANUAL,
    MENU_APP_MODE_AUTO
} MenuApp_Mode_t;

void MenuApp_Init(void);
void MenuApp_Task(void);

#endif
