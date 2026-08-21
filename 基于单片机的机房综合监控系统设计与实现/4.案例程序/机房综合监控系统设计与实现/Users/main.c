#include "main.h"
#include "APPLICATION/Menu/Menu.h"

int main(void)
{
    HAL_Init();

    sys_stm32_clock_init(RCC_PLL_MUL9);
    delay_init(72);

    Menu_Init();

    while (1)
    {
        Menu_Task();
    }
}
