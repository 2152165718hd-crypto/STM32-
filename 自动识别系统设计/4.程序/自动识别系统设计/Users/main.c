#include "main.h"

int main(void)
{
    App_Init();

    while (1)
        App_Loop();
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == NULL)
        return;

    if (huart->Instance == ESP_UART)
        ESP_RxCallback();
    else if (huart->Instance == USART2)
        FM225_RxCallback();
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart == NULL)
        return;

    ESP_ErrorCallback(huart);
    FM225_ErrorCallback(huart);
}
