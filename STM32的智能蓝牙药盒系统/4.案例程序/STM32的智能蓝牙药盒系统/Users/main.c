#include "main.h"

/**
 * @file main.c
 * @brief 程序入口与串口接收回调分发。
 */

/**
 * @brief 系统主函数。
 * @return 理论上不会返回。
 */
int main(void)
{
    /* 系统时钟配置为 72MHz，并初始化延时基准。 */
    sys_stm32_clock_init(RCC_PLL_MUL9);
    delay_init(72);

    /* 初始化应用层菜单与全部业务外设。 */
    Menu_Init();

    while (1)
    {
        Menu_Task();
    }
}

/**
 * @brief HAL 串口接收完成回调。
 * @param huart 触发中断的串口句柄。
 * @note 该回调把 USART1/USART2 的接收事件分别转交给语音模块与蓝牙模块处理。
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        LU6288_RxCpltCallback();
    }

    if (huart->Instance == USART2)
    {
        BLUETOOTH_RxCpltCallback();
    }
}
