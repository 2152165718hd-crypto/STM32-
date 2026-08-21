#include "main.h"

int main(void)
{
    HAL_Init();
    /* ---- 系统时钟与延时初始化 ---- */
    sys_stm32_clock_init(RCC_PLL_MUL9); /* 时钟初始化, 72M */
    delay_init(72);                     /* 延时函数初始化   */

    Menu_SystemInit();

    while (1)
    {
        Menu_SystemLoop();
    }
}
