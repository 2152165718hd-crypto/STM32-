#include "main.h"
#include "stm32f1xx_it.h"

void NMI_Handler(void)
{
}

void HardFault_Handler(void)
{
    while (1)
    {
    }
}

void MemManage_Handler(void)
{
    while (1)
    {
    }
}

void BusFault_Handler(void)
{
    while (1)
    {
    }
}

void UsageFault_Handler(void)
{
    while (1)
    {
    }
}

void SVC_Handler(void)
{
}

void DebugMon_Handler(void)
{
}

void PendSV_Handler(void)
{
}

void SysTick_Handler(void)
{
    HAL_IncTick();
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if ((htim != NULL) && (htim->Instance == TIM2))
    {
        Key_TimerCallback(htim);
    }
}

void DMA1_Channel1_IRQHandler(void)
{
    SecuritySystem_DMA_IRQHandler();
}

void USART1_IRQHandler(void)
{
    ESP_IRQHandler();
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if ((huart != NULL) && (huart->Instance == ESP_UART))
    {
        ESP_RxCallback();
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    ESP_ErrorCallback(huart);
}
