#include "main.h"

int main(void)
{
    HAL_Init();
    sys_stm32_clock_init(RCC_PLL_MUL9);
    delay_init(72);

    ST7735_Init();
    Key_Init();
    Buzzer_Init();
    HR_Sensor_Init();
    IR_Temp_Init();
    ST7735_Backlight(1U);

    Menu_Init();

    while (1)
    {
        Menu_Task();
    }
}
