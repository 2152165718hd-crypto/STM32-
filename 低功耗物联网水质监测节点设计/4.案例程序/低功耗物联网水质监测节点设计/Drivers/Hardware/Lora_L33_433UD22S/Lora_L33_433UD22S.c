#include ".\Hardware\Lora_L33_433UD22S\Lora_L33_433UD22S.h"
#include <string.h>

static UART_HandleTypeDef s_huart3;
static uint8_t s_rx_byte;
static char s_line_buf[LORA_LINE_MAX];
static volatile uint16_t s_line_len = 0U;
static volatile uint8_t s_line_ready = 0U;
static char s_ready_line[LORA_LINE_MAX];

static void Lora_GPIO_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_USART3_CLK_ENABLE();

    gpio.Pin = LORA_TX_PIN;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(LORA_TX_PORT, &gpio);

    gpio.Pin = LORA_RX_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(LORA_RX_PORT, &gpio);

    gpio.Pin = LORA_M0_PIN | LORA_M1_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &gpio);

    gpio.Pin = LORA_AUX_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LORA_AUX_PORT, &gpio);
}

uint8_t Lora_Init(void)
{
    Lora_GPIO_Init();
    Lora_SetMode(LORA_MODE_NORMAL);

    s_huart3.Instance = LORA_UART_INSTANCE;
    s_huart3.Init.BaudRate = LORA_UART_BAUDRATE;
    s_huart3.Init.WordLength = UART_WORDLENGTH_8B;
    s_huart3.Init.StopBits = UART_STOPBITS_1;
    s_huart3.Init.Parity = UART_PARITY_NONE;
    s_huart3.Init.Mode = UART_MODE_TX_RX;
    s_huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    s_huart3.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&s_huart3) != HAL_OK)
    {
        return 0U;
    }

    HAL_NVIC_SetPriority(USART3_IRQn, 2U, 0U);
    HAL_NVIC_EnableIRQ(USART3_IRQn);
    Lora_RestartRxIT();

    return 1U;
}

void Lora_SetMode(Lora_Mode_t mode)
{
    switch (mode)
    {
    case LORA_MODE_WAKE_UP:
        HAL_GPIO_WritePin(LORA_M0_PORT, LORA_M0_PIN, GPIO_PIN_SET);
        HAL_GPIO_WritePin(LORA_M1_PORT, LORA_M1_PIN, GPIO_PIN_RESET);
        break;
    case LORA_MODE_POWER_SAVE:
        HAL_GPIO_WritePin(LORA_M0_PORT, LORA_M0_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(LORA_M1_PORT, LORA_M1_PIN, GPIO_PIN_SET);
        break;
    case LORA_MODE_CONFIG:
        HAL_GPIO_WritePin(LORA_M0_PORT, LORA_M0_PIN, GPIO_PIN_SET);
        HAL_GPIO_WritePin(LORA_M1_PORT, LORA_M1_PIN, GPIO_PIN_SET);
        break;
    case LORA_MODE_NORMAL:
    default:
        HAL_GPIO_WritePin(LORA_M0_PORT, LORA_M0_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(LORA_M1_PORT, LORA_M1_PIN, GPIO_PIN_RESET);
        break;
    }

    HAL_Delay(5U);
}

uint8_t Lora_IsAuxReady(void)
{
    return (HAL_GPIO_ReadPin(LORA_AUX_PORT, LORA_AUX_PIN) == GPIO_PIN_SET) ? 1U : 0U;
}

uint8_t Lora_HasPendingLine(void)
{
    uint8_t ready;

    __disable_irq();
    ready = s_line_ready;
    __enable_irq();

    return ready;
}

HAL_StatusTypeDef Lora_SendBytes(const uint8_t *data, uint16_t len)
{
    uint32_t start = HAL_GetTick();

    if ((data == NULL) || (len == 0U))
    {
        return HAL_ERROR;
    }

    while (Lora_IsAuxReady() == 0U)
    {
        if ((HAL_GetTick() - start) > 1000U)
        {
            break;
        }
    }

    return HAL_UART_Transmit(&s_huart3, (uint8_t *)data, len, 1000U);
}

HAL_StatusTypeDef Lora_SendString(const char *text)
{
    if (text == NULL)
    {
        return HAL_ERROR;
    }

    return Lora_SendBytes((const uint8_t *)text, (uint16_t)strlen(text));
}

uint8_t Lora_ReadLine(char *line, uint16_t max_len)
{
    uint16_t copy_len;

    if ((line == NULL) || (max_len == 0U) || (s_line_ready == 0U))
    {
        return 0U;
    }

    __disable_irq();
    copy_len = (uint16_t)strlen(s_ready_line);
    if (copy_len >= max_len)
    {
        copy_len = (uint16_t)(max_len - 1U);
    }
    memcpy(line, s_ready_line, copy_len);
    line[copy_len] = '\0';
    s_line_ready = 0U;
    __enable_irq();

    return 1U;
}

void Lora_RestartRxIT(void)
{
    (void)HAL_UART_Receive_IT(&s_huart3, &s_rx_byte, 1U);
}

UART_HandleTypeDef *Lora_GetUartHandle(void)
{
    return &s_huart3;
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    char ch;

    if (huart->Instance != LORA_UART_INSTANCE)
    {
        return;
    }

    ch = (char)s_rx_byte;
    if ((ch == '\n') || (ch == '\r'))
    {
        if ((s_line_len > 0U) && (s_line_ready == 0U))
        {
            s_line_buf[s_line_len] = '\0';
            memcpy(s_ready_line, s_line_buf, (uint16_t)(s_line_len + 1U));
            s_line_ready = 1U;
        }
        s_line_len = 0U;
    }
    else if (s_line_len < (LORA_LINE_MAX - 1U))
    {
        s_line_buf[s_line_len++] = ch;
    }
    else
    {
        s_line_len = 0U;
    }

    Lora_RestartRxIT();
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == LORA_UART_INSTANCE)
    {
        Lora_RestartRxIT();
    }
}

void USART3_IRQHandler(void)
{
    HAL_UART_IRQHandler(&s_huart3);
}
