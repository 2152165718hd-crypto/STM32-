#include "main.h"

int main(void)
{
    HAL_Init();
    sys_stm32_clock_init(RCC_PLL_MUL9);
    delay_init(72);

    SecuritySystem_Init();

    while (1)
    {
        SecuritySystem_Task();
    }
}
