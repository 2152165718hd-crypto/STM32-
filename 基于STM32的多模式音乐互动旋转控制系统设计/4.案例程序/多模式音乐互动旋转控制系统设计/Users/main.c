#include "main.h"

int main(void)
{
    HAL_Init();
    sys_stm32_clock_init(RCC_PLL_MUL9);
    delay_init(72);
    MenuApp_Init();


    while (1)
    {
        MenuApp_Task();
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == BT_UART)
    {
        BLUETOOTH_RxCpltCallback();
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == BT_UART)
    {
        BLUETOOTH_ErrorCallback();
    }
}
