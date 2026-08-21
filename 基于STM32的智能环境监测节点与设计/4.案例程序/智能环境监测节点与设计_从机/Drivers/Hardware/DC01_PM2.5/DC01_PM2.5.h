#ifndef __DC01_PM2_5_H__
#define __DC01_PM2_5_H__

#include "stm32f1xx_hal.h"

/* UART basic configuration */
#define DC01_PM2_5_UART USART3
#define DC01_PM2_5_UART_IRQn USART3_IRQn
#define DC01_PM2_5_UART_BAUDRATE 9600U

#define DC01_PM2_5_UART_CLK_ENABLE() __HAL_RCC_USART3_CLK_ENABLE()

#define DC01_PM2_5_UART_TX_PIN GPIO_PIN_10
#define DC01_PM2_5_UART_TX_GPIO_PORT GPIOB
#define DC01_PM2_5_UART_TX_CLK_ENABLE() __HAL_RCC_GPIOB_CLK_ENABLE()

#define DC01_PM2_5_UART_RX_PIN GPIO_PIN_11
#define DC01_PM2_5_UART_RX_GPIO_PORT GPIOB
#define DC01_PM2_5_UART_RX_CLK_ENABLE() __HAL_RCC_GPIOB_CLK_ENABLE()

/* USART3 remap mode:
 * 0U: no remap (PB10/PB11)
 * 1U: partial remap (PC10/PC11)
 * 2U: full remap (PD8/PD9)
 */
#define DC01_PM2_5_UART_REMAP_MODE 0U

/* Protocol definitions */
#define DC01_PM2_5_FRAME_HEADER 0xA5U
#define DC01_PM2_5_FRAME_LEN 4U
#define DC01_PM2_5_DATA_MASK 0x7FU

/* Optical calibration factor, recommended around 0.4 */
#define DC01_PM2_5_DEFAULT_K_FACTOR 0.4f

typedef struct
{
	uint16_t raw_ug_m3;
	float calibrated_ug_m3;
	uint8_t valid;
} DC01_PM2_5_Data_t;

void DC01_PM2_5_Init(void);
uint8_t DC01_PM2_5_IsDataValid(void);
uint16_t DC01_PM2_5_GetRawConcentration(void);
float DC01_PM2_5_GetCalibratedConcentration(void);
void DC01_PM2_5_SetKFactor(float k_factor);
float DC01_PM2_5_GetKFactor(void);
uint8_t DC01_PM2_5_Read(DC01_PM2_5_Data_t *data);


#endif /* __DC01_PM2_5_H__ */
