#include "main.h"

int main(void)
{
    /* ---- 系统时钟与延时初始化 ---- */
    sys_stm32_clock_init(RCC_PLL_MUL9); /* 时钟初始化, 72M */
    delay_init(72);                     /* 延时函数初始化   */

    /* ---- 硬件外设初始化 ---- */
    OLED_Init();          /* OLED 显示屏          */
    Key_Init();           /* 按键模块             */
    Buzzer_Init();        /* 蜂鸣器               */
    LED_Init();           /* LED 指示灯           */
    Vibrator_Init();      /* 振动马达             */
    SoundSensor_Init();   /* 声音传感器 (ADC+DO)  */
    CrySensor_Init();     /* AI 哭声识别模块      */
    Bluetooth_Init();     /* 蓝牙 JDY-31 模块     */

    /* ---- 菜单 / 业务逻辑初始化 ---- */
    Menu_Init();

    /* ---- 主循环 ---- */
    while (1)
    {
        Menu_Task();      /* 业务主任务            */
        delay_ms(100);    /* 主循环周期 ~100ms     */
    }
}
