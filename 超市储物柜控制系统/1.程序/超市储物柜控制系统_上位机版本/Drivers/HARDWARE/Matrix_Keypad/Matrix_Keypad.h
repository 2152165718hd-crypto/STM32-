#ifndef __MATRIX_KEYPAD_H
#define __MATRIX_KEYPAD_H

#include "stm32f4xx_hal.h"
#include <stdint.h>

#define MATRIX_ROW1_PORT GPIOC
#define MATRIX_ROW1_PIN GPIO_PIN_0
#define MATRIX_ROW2_PORT GPIOE
#define MATRIX_ROW2_PIN GPIO_PIN_2
#define MATRIX_ROW3_PORT GPIOE
#define MATRIX_ROW3_PIN GPIO_PIN_4
#define MATRIX_ROW4_PORT GPIOE
#define MATRIX_ROW4_PIN GPIO_PIN_3

#define MATRIX_COL1_PORT GPIOE
#define MATRIX_COL1_PIN GPIO_PIN_6
#define MATRIX_COL2_PORT GPIOC
#define MATRIX_COL2_PIN GPIO_PIN_13
#define MATRIX_COL3_PORT GPIOC
#define MATRIX_COL3_PIN GPIO_PIN_10
#define MATRIX_COL4_PORT GPIOC
#define MATRIX_COL4_PIN GPIO_PIN_11

#ifndef MATRIX_KEY_PRESSED_LEVEL
#define MATRIX_KEY_PRESSED_LEVEL GPIO_PIN_RESET
#endif

typedef struct
{
    char key;
    uint8_t row;
    uint8_t col;
} MatrixKeyEvent_t;

void MatrixKeypad_Init(void);
char MatrixKeypad_GetKey(void);
uint8_t MatrixKeypad_GetEvent(MatrixKeyEvent_t *event);
void MatrixKeypad_TimerCallback(TIM_HandleTypeDef *htim);

#endif
