#include "main.h"

int main(void)
{
    /* ---- 系统时钟与延时初始化 ---- */
    sys_stm32_clock_init(RCC_PLL_MUL9); /* 时钟初始化, 72M */
    delay_init(72);                     /* 延时函数初始化   */

    /* ---- 硬件外设初始化 ---- */
    OLED_Init();             /* OLED 显示屏     */
    DHT11_Init();            /* DHT11 温湿度传感器 */
    DoorWinMagSensor_Init(); /* 门窗磁感应传感器   */
    AV_Alarm_Init();         /* 声光报警器        */
    SR505_Init();            /* SR505 红外人体传感器 */
    Strobe_Init();           /* 强光频闪灯        */
    JQ8400_Init();           /* JQ8400 语音播报模块 */
    Key_Init();              /* 按键模块          */

    /* ---- 菜单系统初始化 ---- */
    Menu_Init();

    /* ---- 主循环 ---- */
    while (1)
    {
        Menu_Loop();
    }
}
