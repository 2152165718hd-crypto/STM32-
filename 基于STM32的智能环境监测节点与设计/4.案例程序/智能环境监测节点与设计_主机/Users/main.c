#include "main.h"

int main(void)
{
    HAL_Init();
    sys_stm32_clock_init(RCC_PLL_MUL9);
    delay_init(72);

    OLED_Init();
    Key_Init();
    Buzzer_Init();
    Zigbee_Init();
    Menu_Init();
    (void)OneNet_Init();
    OneNet_RegisterThresholdCallback(Menu_ApplyCloudThresholds);

    while (1)
    {
        Menu_Run();
        OneNet_Process();
    }
}
