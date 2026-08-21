#include "main.h"

int main(void)
{
    HAL_Init();
    sys_stm32_clock_init(RCC_PLL_MUL9);
    delay_init(72);

    OLED_Init();
    Key_Init();
    LED_Init();
    FAN_Init();
    Buzzer_Init();
    Motor_Init();
    (void)DS18B20_Init();
    (void)VL53L0X_Init();
    (void)ESP_Init();

    Menu_Init();

    while (1)
    {
        Menu_Task();
    }
}
