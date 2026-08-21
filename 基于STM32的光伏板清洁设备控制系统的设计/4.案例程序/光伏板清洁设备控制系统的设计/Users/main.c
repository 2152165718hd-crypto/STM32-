#include "main.h"

int main(void)
{
    HAL_Init();
    sys_stm32_clock_init(RCC_PLL_MUL9);
    delay_init(72);

    (void)PVClean_Init();
    Menu_Init();

    while (1)
    {
        PVClean_Process();
        Menu_Process();
        HAL_Delay(20);
    }
}
