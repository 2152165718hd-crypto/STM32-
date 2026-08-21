#ifndef ST7735_H
#define ST7735_H

#include <stdint.h>
#include <stddef.h>
#include "stm32f1xx_hal.h"
#include ".\Hardware\TFT_ST7735\TFT_Data.h"

#define ST7735_RST_GPIO_Port GPIOA
#define ST7735_RST_Pin GPIO_PIN_8

#define ST7735_DC_GPIO_Port GPIOA
#define ST7735_DC_Pin GPIO_PIN_9
#define ST7735_CS_GPIO_Port GPIOA
#define ST7735_CS_Pin GPIO_PIN_10
#define ST7735_BL_GPIO_Port GPIOA
#define ST7735_BL_Pin GPIO_PIN_11

#define ST7735_SCK_GPIO_Port GPIOB
#define ST7735_SCK_Pin GPIO_PIN_13
#define ST7735_MOSI_GPIO_Port GPIOB
#define ST7735_MOSI_Pin GPIO_PIN_15

#define ST7735_SPI SPI2
#define ST7735_SPI_BAUDRATE_PRESCALER SPI_BAUDRATEPRESCALER_2
/* 1: 软件SPI(任意引脚), 0: 硬件SPI(需匹配ST7735_SPI对应引脚) */
#define ST7735_USE_SOFT_SPI 0
/* 1: 使用DMA发送像素数据, 0: 轮询发送 */
#define ST7735_USE_DMA 1
/* DMA模式下, 小于该字节数仍走轮询, 避免小包DMA开销 */
#define ST7735_DMA_MIN_SIZE 64U
/* DMA发送等待超时(ms) */
#define ST7735_DMA_TIMEOUT_MS 1000U
/* Shared pixel staging buffer. 1024 bytes keeps SRAM use low and cuts full-screen fill DMA chunks to about 40. */
#define ST7735_TX_BUFFER_SIZE 1024U

#if (ST7735_USE_SOFT_SPI == 1) && (ST7735_USE_DMA == 1)
#error "ST7735_USE_DMA requires ST7735_USE_SOFT_SPI to be 0"
#endif

#if ((ST7735_TX_BUFFER_SIZE < 2U) || ((ST7735_TX_BUFFER_SIZE & 1U) != 0U))
#error "ST7735_TX_BUFFER_SIZE must be an even value >= 2"
#endif

#if (ST7735_USE_SOFT_SPI == 0) && (ST7735_USE_DMA == 1)
/* 当前配置为 SPI2: TX DMA 通道为 DMA1_Channel5 */
#define ST7735_SPI_TX_DMA_CHANNEL DMA1_Channel5
#define ST7735_SPI_TX_DMA_IRQn DMA1_Channel5_IRQn
#define ST7735_SPI_TX_DMA_IRQHandler DMA1_Channel5_IRQHandler
#endif

#define ST7735_XSTART 2
#define ST7735_YSTART 1
#define ST7735_WIDTH 128
#define ST7735_HEIGHT 160

// Screen Direction
#define ST7735_ROTATION_0 0U
#define ST7735_ROTATION_90 1U
#define ST7735_ROTATION_180 2U
#define ST7735_ROTATION_270 3U
#define ST7735_ROTATION ST7735_ROTATION_0
/* 轴翻转开关: 1=翻转, 0=不翻转 */
#define ST7735_FLIP_X 1
#define ST7735_FLIP_Y 1
// Color Mode: RGB or BGR
#define ST7735_MADCTL_RGB 0x00
#define ST7735_MADCTL_BGR 0x08
#define ST7735_MADCTL_MODE ST7735_MADCTL_RGB
// Color Inverse: 0=NO, 1=YES
#define ST7735_INVERSE 0

// Backlight active level
#define ST7735_BL_ACTIVE_LEVEL GPIO_PIN_SET

// Color definitions
#define ST7735_BLACK 0x0000
#define ST7735_BLUE 0x001F
#define ST7735_RED 0xF800
#define ST7735_GREEN 0x07E0
#define ST7735_CYAN 0x07FF
#define ST7735_MAGENTA 0xF81F
#define ST7735_YELLOW 0xFFE0
#define ST7735_WHITE 0xFFFF
#define ST7735_COLOR565(r, g, b) (((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3))

void ST7735_Init(void);
void ST7735_SetRotation(uint8_t rotation);
void ST7735_Backlight(uint8_t on);
void ST7735_DrawPixel(uint16_t x, uint16_t y, uint16_t color);
void ST7735_DrawRectangle(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color);
void ST7735_DrawChar(uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bgColor, const FontDef *font);
void ST7735_DrawString(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bgColor, const FontDef *font);
void ST7735_FillScreen(uint16_t color);
void ST7735_DrawImage(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const uint8_t *image);
void ST7735_SPI_DMA_IRQHandler(void);

#endif
