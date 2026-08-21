#include "SYSTEM/sys/sys.h"
#include "SYSTEM/usart/usart.h"
#include "SYSTEM/delay/delay.h"
#include "locker_app.h"

int main(void)
{
    HAL_Init();
    if (sys_stm32_clock_init(336, 8, 2, 7) != 0U)
    {
        __disable_irq();
        NVIC_SystemReset();
        while (1)
        {
        }
    }
    delay_init(168);
    usart_init(115200);

    LockerApp_Init();

    while (1)
    {
        LockerApp_Process();
    }
}
