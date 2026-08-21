#include "main.h"
#include <stdio.h>
#include <string.h>

/* ======================== 主函数 ======================== */

int main(void)
{
    /* 应用层初始化：所有硬件 + 回调注册 + 菜单构建 */
    App_Init();


    /* 主循环 */
    while (1)
    {
        App_Loop();
    }
}

/* ======================== 中断回调 ======================== */

/**
 * @brief HAL UART 接收完成回调
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {
        FM225_RxCallback();
    }
    else if (huart->Instance == USART1)
    {
        ESP_RxCallback();
    }
}

/**
 * @brief HAL UART 错误回调
 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {
        FM225_ErrorCallback(huart);
    }
}
