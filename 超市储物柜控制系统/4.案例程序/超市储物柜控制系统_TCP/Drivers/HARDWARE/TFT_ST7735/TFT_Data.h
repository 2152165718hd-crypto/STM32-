#ifndef __TFT_DATA_H__
#define __TFT_DATA_H__

#include <stdint.h>

typedef struct
{
	const uint8_t width;
	const uint8_t height;
	const uint32_t *data;
} FontDef;

extern const FontDef Font_7x10;
extern const FontDef Font_11x18;
extern const FontDef Font_Custom;

#endif
