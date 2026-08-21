#include "main.h"

int main(void)
{
    HAL_Init();
    sys_stm32_clock_init(RCC_PLL_MUL9);
    delay_init(72);

    OLED_Init();
    SlaveApp_Init();

    while (1)
    {
        SlaveApp_Task();
    }
}
