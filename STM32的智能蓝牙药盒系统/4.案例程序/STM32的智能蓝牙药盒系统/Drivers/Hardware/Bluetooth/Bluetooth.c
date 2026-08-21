#include ".\Hardware\Bluetooth\Bluetooth.h"
#include <string.h>
#include <stdio.h>

/**
 * @file Bluetooth.c
 * @brief JDY-31 蓝牙串口模块驱动实现。
 */

/* ========== 私有变量 ========== */
static UART_HandleTypeDef huart_bt; /* USART2 句柄           */
static uint8_t bt_rx_byte;          /* 单字节中断接收缓冲    */

/* 接收环形缓冲区 */
static uint8_t bt_rx_buf[BT_RX_BUF_SIZE];
static volatile uint16_t bt_rx_head = 0; /* 写入位置 (ISR 侧)   */
static volatile uint16_t bt_rx_tail = 0; /* 读出位置 (应用侧)   */


#define BT_NAME "donk666"
#define BT_PIN "6666"

/* ==================================================================
 *  公共函数实现
 * ================================================================== */

/**
 * @brief  初始化 USART2 (9600-8-N-1)、GPIO、NVIC 并开启接收中断
 */
void Bluetooth_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* 1. 使能外设时钟 */
    __HAL_RCC_USART2_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* 2. 配置 PA2 (TX) —— 复用推挽输出 */
    GPIO_InitStruct.Pin = BT_TX_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(BT_GPIO_PORT, &GPIO_InitStruct);

    /* 3. 配置 PA3 (RX) —— 浮空输入 */
    GPIO_InitStruct.Pin = BT_RX_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(BT_GPIO_PORT, &GPIO_InitStruct);

    /* 4. 配置 USART2 */
    huart_bt.Instance = BT_UART;
    huart_bt.Init.BaudRate = BT_BAUDRATE;
    huart_bt.Init.WordLength = UART_WORDLENGTH_8B;
    huart_bt.Init.StopBits = UART_STOPBITS_1;
    huart_bt.Init.Parity = UART_PARITY_NONE;
    huart_bt.Init.Mode = UART_MODE_TX_RX;
    huart_bt.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart_bt.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart_bt);

    /* 5. 使能 USART2 中断 */
    HAL_NVIC_SetPriority(USART2_IRQn, 3, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);

    /* 6. 开启接收中断（单字节方式） */
    HAL_UART_Receive_IT(&huart_bt, &bt_rx_byte, 1);


}

/**
 * @brief  发送数据（阻塞，超时 200ms）
 */
void Bluetooth_Send(const uint8_t *data, uint16_t len)
{
    HAL_UART_Transmit(&huart_bt, (uint8_t *)data, len, 200);
}

/**
 * @brief  发送字符串
 */
void Bluetooth_SendString(const char *str)
{
    Bluetooth_Send((const uint8_t *)str, (uint16_t)strlen(str));
}

/**
 * @brief  获取接收缓冲区中可读字节数
 */
uint16_t Bluetooth_Available(void)
{
    uint16_t head = bt_rx_head;
    uint16_t tail = bt_rx_tail;
    return (uint16_t)((head - tail + BT_RX_BUF_SIZE) % BT_RX_BUF_SIZE);
}

/**
 * @brief  从接收缓冲区读取一个字节
 */
uint8_t Bluetooth_ReadByte(uint8_t *byte)
{
    if (bt_rx_head == bt_rx_tail)
        return 0; /* 缓冲区为空 */

    *byte = bt_rx_buf[bt_rx_tail];
    bt_rx_tail = (bt_rx_tail + 1) % BT_RX_BUF_SIZE;
    return 1;
}

/**
 * @brief  从接收缓冲区读取多个字节
 */
uint16_t Bluetooth_Read(uint8_t *buf, uint16_t maxLen)
{
    uint16_t count = 0;
    while (count < maxLen && bt_rx_head != bt_rx_tail)
    {
        buf[count++] = bt_rx_buf[bt_rx_tail];
        bt_rx_tail = (bt_rx_tail + 1) % BT_RX_BUF_SIZE;
    }
    return count;
}

/**
 * @brief  清空接收缓冲区
 */
void Bluetooth_FlushRx(void)
{
    bt_rx_tail = bt_rx_head;
}

/**
 * @brief  UART 接收完成回调（由用户在 HAL_UART_RxCpltCallback 中调用）
 *         将接收到的字节存入环形缓冲区，并重新开启接收中断
 */
void BLUETOOTH_RxCpltCallback(void)
{
    /* 将接收到的字节写入环形缓冲区 */
    uint16_t next = (bt_rx_head + 1) % BT_RX_BUF_SIZE;
    if (next != bt_rx_tail) /* 缓冲区未满 */
    {
        bt_rx_buf[bt_rx_head] = bt_rx_byte;
        bt_rx_head = next;
    }

    /* 重新开启单字节接收中断 */
    HAL_UART_Receive_IT(&huart_bt, &bt_rx_byte, 1);
}

/* ==================================================================
 *  AT 指令配置 API
 * ================================================================== */

/**
 * @brief  发送 AT 指令并等待应答（阻塞）
 */
uint8_t Bluetooth_AT_Send(const char *cmd, char *resp, uint16_t respSize)
{
    char buf[BT_AT_RESP_SIZE];
    uint16_t idx = 0;
    uint32_t start;

    /* 清空接收缓冲区，确保读到的是本次应答 */
    Bluetooth_FlushRx();

    /* 发送 AT 指令 + \r\n */
    Bluetooth_SendString(cmd);
    Bluetooth_SendString("\r\n");

    /* 等待应答，直到超时 */
    start = HAL_GetTick();
    while ((HAL_GetTick() - start) < BT_AT_TIMEOUT_MS)
    {
        uint8_t ch;
        while (Bluetooth_ReadByte(&ch))
        {
            if (idx < sizeof(buf) - 1)
                buf[idx++] = (char)ch;
        }
        /* JDY-31 应答以 \r\n 结尾 */
        if (idx >= 2 && buf[idx - 2] == '\r' && buf[idx - 1] == '\n')
            break;
    }
    buf[idx] = '\0';

    if (idx == 0)
        return 0; /* 超时无应答 */

    /* 拷贝应答到用户缓冲区 */
    if (resp != NULL && respSize > 0)
    {
        uint16_t copyLen = idx < (respSize - 1) ? idx : (respSize - 1);
        memcpy(resp, buf, copyLen);
        resp[copyLen] = '\0';
    }

    return 1;
}

/**
 * @brief  测试通信
 */
uint8_t Bluetooth_AT_Test(void)
{
    char resp[BT_AT_RESP_SIZE];
    if (!Bluetooth_AT_Send("AT", resp, sizeof(resp)))
        return 0;
    return (strstr(resp, "+OK") != NULL) ? 1 : 0;
}

/**
 * @brief  查询固件版本
 */
uint8_t Bluetooth_AT_GetVersion(char *ver, uint16_t verSize)
{
    return Bluetooth_AT_Send("AT+VERSION", ver, verSize);
}

/**
 * @brief  查询 MAC 地址
 */
uint8_t Bluetooth_AT_GetAddr(char *addr, uint16_t addrSize)
{
    return Bluetooth_AT_Send("AT+LADDR", addr, addrSize);
}

/**
 * @brief  设置蓝牙广播名称
 */
uint8_t Bluetooth_AT_SetName(const char *name)
{
    char cmd[32];
    char resp[BT_AT_RESP_SIZE];
    snprintf(cmd, sizeof(cmd), "AT+NAME%s", name);
    if (!Bluetooth_AT_Send(cmd, resp, sizeof(resp)))
        return 0;
    return (strstr(resp, "+OK") != NULL) ? 1 : 0;
}

/**
 * @brief  设置配对密码
 */
uint8_t Bluetooth_AT_SetPIN(const char *pin)
{
    char cmd[16];
    char resp[BT_AT_RESP_SIZE];
    snprintf(cmd, sizeof(cmd), "AT+PIN%s", pin);
    if (!Bluetooth_AT_Send(cmd, resp, sizeof(resp)))
        return 0;
    return (strstr(resp, "+OK") != NULL) ? 1 : 0;
}

/**
 * @brief  设置波特率
 */
uint8_t Bluetooth_AT_SetBaud(BT_Baud_t baud)
{
    char cmd[16];
    char resp[BT_AT_RESP_SIZE];
    snprintf(cmd, sizeof(cmd), "AT+BAUD%d", (int)baud);
    if (!Bluetooth_AT_Send(cmd, resp, sizeof(resp)))
        return 0;
    return (strstr(resp, "+OK") != NULL) ? 1 : 0;
}

/**
 * @brief  软件复位模块
 */
uint8_t Bluetooth_AT_Reset(void)
{
    char resp[BT_AT_RESP_SIZE];
    if (!Bluetooth_AT_Send("AT+RESET", resp, sizeof(resp)))
        return 0;
    return (strstr(resp, "+OK") != NULL) ? 1 : 0;
}

/**
 * @brief  恢复出厂设置
 */
uint8_t Bluetooth_AT_Default(void)
{
    char resp[BT_AT_RESP_SIZE];
    if (!Bluetooth_AT_Send("AT+DEFAULT", resp, sizeof(resp)))
        return 0;
    return (strstr(resp, "+OK") != NULL) ? 1 : 0;
}

/**
 * @brief  断开当前蓝牙连接
 */
uint8_t Bluetooth_AT_Disconnect(void)
{
    char resp[BT_AT_RESP_SIZE];
    if (!Bluetooth_AT_Send("AT+DISC", resp, sizeof(resp)))
        return 0;
    return (strstr(resp, "+OK") != NULL) ? 1 : 0;
}

/* ==================================================================
 *  USART2 中断入口（模块自包含）
 * ================================================================== */
void USART2_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart_bt);
}
