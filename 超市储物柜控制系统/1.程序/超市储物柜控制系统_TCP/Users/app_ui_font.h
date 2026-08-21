#ifndef __APP_UI_FONT_H
#define __APP_UI_FONT_H

#include "stm32f4xx_hal.h"
#include <stdint.h>

void AppFont_DrawText(uint16_t x, uint16_t y, const char *utf8, uint16_t color, uint16_t bgColor);
uint16_t AppFont_TextWidth(const char *utf8);

#endif
