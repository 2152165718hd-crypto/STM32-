#include "main.h"

int main(void)
{
    HAL_Init();
    sys_stm32_clock_init(RCC_PLL_MUL9);
    delay_init(72);

    OLED_Init();
    Key_Init();
    GatewayApp_Init();

    while (1)
    {
			GatewayApp_Task();
    }
}
