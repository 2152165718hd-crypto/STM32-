#ifndef __OLED_H
#define __OLED_H

#include <stdint.h>
#include ".\Hardware\OLED\OLED_Data.h"
#include "stm32f1xx_hal.h"

/* User config */
#define OLED_USE_HARDWARE_I2C 0 /* 1: hardware I2C, 0: software I2C */
#define OLED_USE_DMA 0          /* 1: enable DMA transfer, 0: disable DMA transfer */
#define OLED_DMA_TRANSPORT_MODE 0 /* 0: DMA memory transport + blocking I2C (stable), 1: I2C DMA transport */
#define OLED_ENABLE_DIFF_UPDATE 1 /* 1: update changed bytes only, 0: always refresh requested area */

#define OLED_SCL_PIN GPIO_PIN_12
#define OLED_SCL_PORT GPIOB
#define OLED_SDA_PIN GPIO_PIN_13
#define OLED_SDA_PORT GPIOB

#if !OLED_USE_HARDWARE_I2C
#undef OLED_USE_DMA
#define OLED_USE_DMA 0
#endif

#if OLED_USE_HARDWARE_I2C
#define OLED_I2C_INSTANCE hi2c1
extern I2C_HandleTypeDef OLED_I2C_INSTANCE;
#define OLED_I2C_ADDR 0x78
#define OLED_I2C_CLOCK_SPEED 400000
#define OLED_I2C_TIMEOUT 100
#define OLED_DMA_WAIT_TIMEOUT 20
#if OLED_USE_DMA
#if OLED_DMA_TRANSPORT_MODE
extern DMA_HandleTypeDef hdma_i2c1_tx;
#else
extern DMA_HandleTypeDef hdma_oled_memcpy;
#endif
#endif
#endif

#define OLED_8X16 8
#define OLED_6X8 6

#define OLED_UNFILLED 0
#define OLED_FILLED 1

void OLED_Init(void);

void OLED_Update(void);
void OLED_UpdateArea(int16_t X, int16_t Y, uint8_t Width, uint8_t Height);

void OLED_Clear(void);
void OLED_ClearArea(int16_t X, int16_t Y, uint8_t Width, uint8_t Height);
void OLED_Reverse(void);
void OLED_ReverseArea(int16_t X, int16_t Y, uint8_t Width, uint8_t Height);

void OLED_ShowChar(int16_t X, int16_t Y, char Char, uint8_t FontSize);
void OLED_ShowString(int16_t X, int16_t Y, char *String, uint8_t FontSize);
void OLED_ShowNum(int16_t X, int16_t Y, uint32_t Number, uint8_t Length, uint8_t FontSize);
void OLED_ShowSignedNum(int16_t X, int16_t Y, int32_t Number, uint8_t Length, uint8_t FontSize);
void OLED_ShowHexNum(int16_t X, int16_t Y, uint32_t Number, uint8_t Length, uint8_t FontSize);
void OLED_ShowBinNum(int16_t X, int16_t Y, uint32_t Number, uint8_t Length, uint8_t FontSize);
void OLED_ShowFloatNum(int16_t X, int16_t Y, double Number, uint8_t IntLength, uint8_t FraLength, uint8_t FontSize);
void OLED_ShowImage(int16_t X, int16_t Y, uint8_t Width, uint8_t Height, const uint8_t *Image);
void OLED_Printf(int16_t X, int16_t Y, uint8_t FontSize, char *format, ...);

void OLED_DrawPoint(int16_t X, int16_t Y);
uint8_t OLED_GetPoint(int16_t X, int16_t Y);
void OLED_DrawLine(int16_t X0, int16_t Y0, int16_t X1, int16_t Y1);
void OLED_DrawRectangle(int16_t X, int16_t Y, uint8_t Width, uint8_t Height, uint8_t IsFilled);
void OLED_DrawTriangle(int16_t X0, int16_t Y0, int16_t X1, int16_t Y1, int16_t X2, int16_t Y2, uint8_t IsFilled);
void OLED_DrawCircle(int16_t X, int16_t Y, uint8_t Radius, uint8_t IsFilled);
void OLED_DrawEllipse(int16_t X, int16_t Y, uint8_t A, uint8_t B, uint8_t IsFilled);
void OLED_DrawArc(int16_t X, int16_t Y, uint8_t Radius, int16_t StartAngle, int16_t EndAngle, uint8_t IsFilled);

#endif
