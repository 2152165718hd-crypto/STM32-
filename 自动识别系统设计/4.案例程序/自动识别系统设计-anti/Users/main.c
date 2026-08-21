#include "main.h"

int main(void)
{
    /* 1. HAL 基础初始化 */
    HAL_Init();
    sys_stm32_clock_init(RCC_PLL_MUL9);
    delay_init(72);

    /* 2. 外设初始化 */
    OLED_Init();
    Key_Init();
    Buzzer_Init();
    Relay_Init();
    FM225_Init();

    /* 3. ESP WiFi 初始化 */
    ESP_Status_t esp_ret = ESP_Init();
    ESP_RegisterCallback(Menu_ESP_DataCallback);

    /* 4. 菜单系统初始化（内部注册 FM225 回调 + MF_Start） */
    Menu_Init();
    Menu_SetESPInitOK(esp_ret == ESP_OK ? 1 : 0);

    /* 5. 主循环 */
    while (1)
    {
        FM225_Process();   /* 人脸模块数据处理     */
        ESP_Process();     /* WiFi 数据处理        */
        Menu_Tick();       /* 蜂鸣器报警等定时任务 */
        MF_Loop();         /* 菜单按键扫描 + 渲染  */
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == NULL)
    {
        return;
    }

    if (huart->Instance == ESP_UART)
    {
        ESP_RxCallback();
    }
    else if (huart->Instance == FM225_UART)
    {
        FM225_RxCallback();
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart == NULL)
    {
        return;
    }

    if (huart->Instance == ESP_UART)
    {
        ESP_ErrorCallback(huart);
    }
    else if (huart->Instance == FM225_UART)
    {
        FM225_ErrorCallback(huart);
    }
}
