#include "HARDWARE/Matrix_Keypad/Matrix_Keypad.h"
#include "HARDWARE/Independent_Key/KEY.h"

#define MATRIX_ROW_COUNT 4U
#define MATRIX_COL_COUNT 4U
#define MATRIX_KEY_COUNT (MATRIX_ROW_COUNT * MATRIX_COL_COUNT)
#define MATRIX_DEBOUNCE_CNT 2U
#define MATRIX_EVENT_BUF_SIZE 16U

typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pin;
} MatrixPin_t;

typedef struct
{
    uint8_t stable;
    uint8_t last;
    uint8_t cnt;
} MatrixState_t;

static const MatrixPin_t s_rows[MATRIX_ROW_COUNT] = {
    {MATRIX_ROW1_PORT, MATRIX_ROW1_PIN},
    {MATRIX_ROW2_PORT, MATRIX_ROW2_PIN},
    {MATRIX_ROW3_PORT, MATRIX_ROW3_PIN},
    {MATRIX_ROW4_PORT, MATRIX_ROW4_PIN},
};

static const MatrixPin_t s_cols[MATRIX_COL_COUNT] = {
    {MATRIX_COL1_PORT, MATRIX_COL1_PIN},
    {MATRIX_COL2_PORT, MATRIX_COL2_PIN},
    {MATRIX_COL3_PORT, MATRIX_COL3_PIN},
    {MATRIX_COL4_PORT, MATRIX_COL4_PIN},
};

static const char s_keyMap[MATRIX_ROW_COUNT][MATRIX_COL_COUNT] = {
    {'1', '4', '7', '*'},
    {'2', '5', '8', '0'},
    {'3', '6', '9', '#'},
    {'A', 'B', 'C', 'D'},
};

static MatrixState_t s_state[MATRIX_KEY_COUNT];
static volatile MatrixKeyEvent_t s_evBuf[MATRIX_EVENT_BUF_SIZE];
static volatile uint8_t s_evHead = 0U;
static volatile uint8_t s_evTail = 0U;

static void Matrix_EnablePortClock(GPIO_TypeDef *port)
{
    if (port == GPIOA)
    {
        __HAL_RCC_GPIOA_CLK_ENABLE();
    }
    else if (port == GPIOB)
    {
        __HAL_RCC_GPIOB_CLK_ENABLE();
    }
    else if (port == GPIOC)
    {
        __HAL_RCC_GPIOC_CLK_ENABLE();
    }
    else if (port == GPIOD)
    {
        __HAL_RCC_GPIOD_CLK_ENABLE();
    }
    else if (port == GPIOE)
    {
        __HAL_RCC_GPIOE_CLK_ENABLE();
    }
}

static void Matrix_PushEvent(uint8_t row, uint8_t col)
{
    uint8_t next = (uint8_t)((s_evTail + 1U) % MATRIX_EVENT_BUF_SIZE);

    if (next == s_evHead)
    {
        s_evHead = (uint8_t)((s_evHead + 1U) % MATRIX_EVENT_BUF_SIZE);
    }

    s_evBuf[s_evTail].key = s_keyMap[row][col];
    s_evBuf[s_evTail].row = row;
    s_evBuf[s_evTail].col = col;
    s_evTail = next;
}

static void Matrix_SetAllColumns(GPIO_PinState level)
{
    uint8_t col;
    for (col = 0U; col < MATRIX_COL_COUNT; col++)
    {
        HAL_GPIO_WritePin(s_cols[col].port, s_cols[col].pin, level);
    }
}

void MatrixKeypad_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    uint8_t i;

    for (i = 0U; i < MATRIX_ROW_COUNT; i++)
    {
        Matrix_EnablePortClock(s_rows[i].port);
    }
    for (i = 0U; i < MATRIX_COL_COUNT; i++)
    {
        Matrix_EnablePortClock(s_cols[i].port);
    }

    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    for (i = 0U; i < MATRIX_ROW_COUNT; i++)
    {
        GPIO_InitStruct.Pin = s_rows[i].pin;
        HAL_GPIO_Init(s_rows[i].port, &GPIO_InitStruct);
    }

    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    for (i = 0U; i < MATRIX_COL_COUNT; i++)
    {
        GPIO_InitStruct.Pin = s_cols[i].pin;
        HAL_GPIO_Init(s_cols[i].port, &GPIO_InitStruct);
    }

    Matrix_SetAllColumns(GPIO_PIN_SET);
}

uint8_t MatrixKeypad_GetEvent(MatrixKeyEvent_t *event)
{
    if ((event == NULL) || (s_evHead == s_evTail))
    {
        return 0U;
    }

    __disable_irq();
    if (s_evHead == s_evTail)
    {
        __enable_irq();
        return 0U;
    }
    *event = s_evBuf[s_evHead];
    s_evHead = (uint8_t)((s_evHead + 1U) % MATRIX_EVENT_BUF_SIZE);
    __enable_irq();

    return 1U;
}

char MatrixKeypad_GetKey(void)
{
    MatrixKeyEvent_t ev;

    if (MatrixKeypad_GetEvent(&ev) == 0U)
    {
        return '\0';
    }

    return ev.key;
}

void MatrixKeypad_TimerCallback(TIM_HandleTypeDef *htim)
{
    uint8_t row;
    uint8_t col;

    if ((htim == NULL) || (htim->Instance != KEY_SCAN_TIM_INSTANCE))
    {
        return;
    }

    for (col = 0U; col < MATRIX_COL_COUNT; col++)
    {
        Matrix_SetAllColumns(GPIO_PIN_SET);
        HAL_GPIO_WritePin(s_cols[col].port, s_cols[col].pin, GPIO_PIN_RESET);

        for (row = 0U; row < MATRIX_ROW_COUNT; row++)
        {
            uint8_t index = (uint8_t)(row * MATRIX_COL_COUNT + col);
            uint8_t raw = (HAL_GPIO_ReadPin(s_rows[row].port, s_rows[row].pin) == MATRIX_KEY_PRESSED_LEVEL) ? 1U : 0U;

            if (raw != s_state[index].last)
            {
                s_state[index].last = raw;
                s_state[index].cnt = 0U;
            }
            else if (s_state[index].cnt < MATRIX_DEBOUNCE_CNT)
            {
                s_state[index].cnt++;
                if ((s_state[index].cnt == MATRIX_DEBOUNCE_CNT) && (raw != s_state[index].stable))
                {
                    s_state[index].stable = raw;
                    if (raw != 0U)
                    {
                        Matrix_PushEvent(row, col);
                    }
                }
            }
        }
    }

    Matrix_SetAllColumns(GPIO_PIN_SET);
}
