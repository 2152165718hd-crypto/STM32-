#include ".\Hardware\LU6288\LU6288.h"
#include <stdio.h>
#include <string.h>

/**
 * @file LU6288.c
 * @brief LU6288 语音播报模块驱动实现。
 */

#define LU6288_BAUDRATE 9600U
#define LU6288_BUSY_ASSERT_TIMEOUT_MS 100U

/** @brief 语音发送队列中的一条消息。 */
typedef struct
{
    char data[LU6288_MAX_TEXT_LEN + 16U];
    uint16_t len;
} LU6288_Msg_t;

/** @brief 语音模块忙状态机。 */
typedef enum
{
    LU6288_STATE_IDLE = 0U,
    LU6288_STATE_WAIT_BUSY_ASSERT,
    LU6288_STATE_WAIT_BUSY_RELEASE
} LU6288_State_t;

static UART_HandleTypeDef huart_lu6288;
static uint8_t lu6288_rx_byte;

static LU6288_Msg_t lu6288_queue[LU6288_QUEUE_SIZE];
static volatile uint8_t lu6288_q_head = 0U;
static volatile uint8_t lu6288_q_tail = 0U;
static volatile LU6288_State_t lu6288_state = LU6288_STATE_IDLE;
static volatile uint32_t lu6288_state_tick = 0U;

static uint8_t LU6288_IsBusyPinActive(void);
static void LU6288_StartWaitBusy(void);
static void LU6288_SendRaw(const char *data, uint16_t len);
static uint8_t LU6288_QueuePush(const char *data, uint16_t len);
static uint8_t LU6288_QueuePop(LU6288_Msg_t *msg);

/**
 * @brief 初始化语音模块串口、BUSY 引脚与发送状态机。
 */
void LU6288_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitStruct.Pin = LU6288_TX_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(LU6288_GPIO_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = LU6288_RX_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(LU6288_GPIO_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = LU6288_BUSY_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(LU6288_BUSY_GPIO_PORT, &GPIO_InitStruct);

    huart_lu6288.Instance = USART1;
    huart_lu6288.Init.BaudRate = LU6288_BAUDRATE;
    huart_lu6288.Init.WordLength = UART_WORDLENGTH_8B;
    huart_lu6288.Init.StopBits = UART_STOPBITS_1;
    huart_lu6288.Init.Parity = UART_PARITY_NONE;
    huart_lu6288.Init.Mode = UART_MODE_TX_RX;
    huart_lu6288.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart_lu6288.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart_lu6288);

    HAL_NVIC_SetPriority(USART1_IRQn, 3, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);

    lu6288_q_head = 0U;
    lu6288_q_tail = 0U;
    lu6288_state = LU6288_IsBusyPinActive() ? LU6288_STATE_WAIT_BUSY_RELEASE : LU6288_STATE_IDLE;
    lu6288_state_tick = HAL_GetTick();

    HAL_UART_Receive_IT(&huart_lu6288, &lu6288_rx_byte, 1U);
}

/**
 * @brief 驱动语音模块发送状态机。
 * @note 该函数需要在主循环中周期调用，用于在 BUSY 释放后发送下一条排队消息。
 */
void LU6288_Process(void)
{
    LU6288_Msg_t msg;

    switch (lu6288_state)
    {
    case LU6288_STATE_IDLE:
        if (LU6288_IsBusyPinActive())
        {
            lu6288_state = LU6288_STATE_WAIT_BUSY_RELEASE;
            lu6288_state_tick = HAL_GetTick();
            return;
        }
        break;

    case LU6288_STATE_WAIT_BUSY_ASSERT:
        if (LU6288_IsBusyPinActive())
        {
            lu6288_state = LU6288_STATE_WAIT_BUSY_RELEASE;
            return;
        }
        if ((HAL_GetTick() - lu6288_state_tick) < LU6288_BUSY_ASSERT_TIMEOUT_MS)
            return;
        lu6288_state = LU6288_STATE_IDLE;
        break;

    case LU6288_STATE_WAIT_BUSY_RELEASE:
        if (LU6288_IsBusyPinActive())
            return;
        lu6288_state = LU6288_STATE_IDLE;
        break;

    default:
        lu6288_state = LU6288_STATE_IDLE;
        break;
    }

    if (LU6288_QueuePop(&msg) == 0U)
        return;

    LU6288_SendRaw(msg.data, msg.len);
    LU6288_StartWaitBusy();
}

/**
 * @brief 串口接收完成回调。
 */
void LU6288_RxCpltCallback(void)
{
    HAL_UART_Receive_IT(&huart_lu6288, &lu6288_rx_byte, 1U);
}

/**
 * @brief 将一段文本加入播报队列。
 * @param text 待播报文本。
 */
void LU6288_Speak(const char *text)
{
    char buf[LU6288_MAX_TEXT_LEN + 16U];
    int len;

    if (text == NULL)
        return;

    len = snprintf(buf, sizeof(buf), "<G>%s", text);
    if (len < 0)
        return;
    if ((uint16_t)len >= (uint16_t)sizeof(buf))
        len = (int)sizeof(buf) - 1;

    LU6288_QueuePush(buf, (uint16_t)len);
}

/**
 * @brief 停止当前播报并清空待播报队列。
 */
void LU6288_Stop(void)
{
    lu6288_q_head = lu6288_q_tail;

    LU6288_SendRaw("<C>", 3U);
    lu6288_state_tick = HAL_GetTick();
    lu6288_state = LU6288_IsBusyPinActive() ? LU6288_STATE_WAIT_BUSY_RELEASE : LU6288_STATE_IDLE;
}

/**
 * @brief 将一条背景音乐播放指令加入队列。
 * @param id 背景音乐编号。
 */
void LU6288_PlayBGM(uint8_t id)
{
    char buf[8];
    int len = snprintf(buf, sizeof(buf), "<M>%u", id);

    if (len > 0)
        LU6288_QueuePush(buf, (uint16_t)len);
}

/**
 * @brief 将一条提示音播放指令加入队列。
 * @param id 提示音编号。
 */
void LU6288_PlayTone(uint8_t id)
{
    char buf[8];
    int len = snprintf(buf, sizeof(buf), "<I>%u", id);

    if (len > 0)
        LU6288_QueuePush(buf, (uint16_t)len);
}

/**
 * @brief 查询模块当前是否仍有播报任务。
 * @return 忙返回 `1`，空闲返回 `0`。
 */
uint8_t LU6288_IsBusy(void)
{
    if (LU6288_IsBusyPinActive())
        return 1U;

    if (lu6288_state != LU6288_STATE_IDLE)
        return 1U;

    return (lu6288_q_head != lu6288_q_tail) ? 1U : 0U;
}

/**
 * @brief USART1 中断服务函数。
 */
void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart_lu6288);
}

/**
 * @brief 读取 BUSY 引脚的当前有效状态。
 * @return 忙返回 `1`，空闲返回 `0`。
 */
static uint8_t LU6288_IsBusyPinActive(void)
{
    return (HAL_GPIO_ReadPin(LU6288_BUSY_GPIO_PORT, LU6288_BUSY_PIN) == LU6288_BUSY_ACTIVE_LEVEL) ? 1U : 0U;
}

/**
 * @brief 发送指令后切换到等待 BUSY 响应的状态。
 */
static void LU6288_StartWaitBusy(void)
{
    lu6288_state = LU6288_STATE_WAIT_BUSY_ASSERT;
    lu6288_state_tick = HAL_GetTick();

    if (LU6288_IsBusyPinActive())
        lu6288_state = LU6288_STATE_WAIT_BUSY_RELEASE;
}

/**
 * @brief 直接通过串口发送一帧原始控制字符串。
 * @param data 待发送数据。
 * @param len 发送长度。
 */
static void LU6288_SendRaw(const char *data, uint16_t len)
{
    HAL_UART_Transmit(&huart_lu6288, (uint8_t *)data, len, 500U);
}

/**
 * @brief 向发送队列压入一条消息。
 * @param data 输入数据。
 * @param len 输入长度。
 * @return 入队成功返回 `1`，队列满时返回 `0`。
 */
static uint8_t LU6288_QueuePush(const char *data, uint16_t len)
{
    uint8_t next = (uint8_t)((lu6288_q_tail + 1U) % LU6288_QUEUE_SIZE);

    if (next == lu6288_q_head)
        return 0U;

    if (len > (uint16_t)(sizeof(lu6288_queue[0].data) - 1U))
        len = (uint16_t)(sizeof(lu6288_queue[0].data) - 1U);

    memcpy(lu6288_queue[lu6288_q_tail].data, data, len);
    lu6288_queue[lu6288_q_tail].data[len] = '\0';
    lu6288_queue[lu6288_q_tail].len = len;
    lu6288_q_tail = next;
    return 1U;
}

/**
 * @brief 从发送队列弹出一条消息。
 * @param msg 输出消息对象。
 * @return 出队成功返回 `1`，队列空时返回 `0`。
 */
static uint8_t LU6288_QueuePop(LU6288_Msg_t *msg)
{
    if (lu6288_q_head == lu6288_q_tail)
        return 0U;

    *msg = lu6288_queue[lu6288_q_head];
    lu6288_q_head = (uint8_t)((lu6288_q_head + 1U) % LU6288_QUEUE_SIZE);
    return 1U;
}
