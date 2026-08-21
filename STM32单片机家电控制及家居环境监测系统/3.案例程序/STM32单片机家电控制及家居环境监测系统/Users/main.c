#include "main.h"

int main(void)
{
    /* ======== 1. 系统时钟与延时 ======== */
    sys_stm32_clock_init(RCC_PLL_MUL9);   /* 72MHz */
    delay_init(72);

    /* ======== 2. 硬件外设初始化 ======== */
    DS18B20_Init();       /* DS18B20 温度传感器 */
    OLED_Init();          /* OLED 显示屏 */
    LED_Init();           /* 6路 LED */
    Key_Init();           /* 4路按键 (UP/DOWN/ENTER/BACK) */
    LightSensor_Init();   /* 光敏传感器 ADC */
    SoundSensor_Init();   /* 声音传感器 ADC */
    RainSensor_Init();    /* 雨滴传感器 ADC */
    Servo_Init();         /* 舵机 (窗户控制) */
    Motor_Init();         /* 电机 (窗帘控制) */
    Relay_Init();         /* 继电器 (空调/电饭煲) */

    /* ======== 3. ESP-01S WiFi 模块 ======== */
    if (ESP_Init() == ESP_OK)
    {
        ESP_RegisterCallback(Menu_OnESPData);
    }

    /* ======== 4. 菜单系统初始化 ======== */
    Menu_Init();

    /* ======== 5. 主循环 ======== */
    while (1)
    {
        Menu_BusinessLogic();   /* 传感器采集 + 自动控制 + 倒计时 + ESP上报 */
        ESP_Process();          /* ESP 接收数据解析 */
        MF_Loop();              /* 菜单按键扫描 + 渲染 + OLED刷新 */
    }
}
          

 