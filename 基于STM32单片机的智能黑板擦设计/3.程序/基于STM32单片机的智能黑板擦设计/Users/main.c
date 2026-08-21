#include "main.h"
#include <stdio.h>

/**
 * @brief  智能黑板擦 — 主函数
 *
 *  主循环节奏 (50ms 基础周期):
 *    每轮:  按键扫描 → MF_Process → 蓝牙命令处理 → 执行器同步 → MF_Render
 *    200ms: 传感器采集 → 自动控制逻辑
 *    1000ms: 蓝牙数据上报
 */
int main(void)
{
    uint16_t sensor_tick = 0;
    uint16_t bt_tick     = 0;
    KeyEvent_t key;
    uint8_t back_latch = 0;

    /* ==================== 系统初始化 ==================== */
    sys_stm32_clock_init(RCC_PLL_MUL9);
    delay_init(72);

    /* ==================== 硬件初始化 ==================== */
    OLED_Init();
    Key_Init();
    TB6612_Init();
    TB6612_StopAll();
    Bluetooth_Init();
    DC01_Init(9600);
    WaterLevel_Init();
    FAN_Init();
    Mist_Module_Init();

    /* ==================== 菜单初始化 ==================== */
    Menu_Init();

    /* ==================== 开机画面 ==================== */
    OLED_Clear();
    OLED_ShowString(8, 8,  "Smart Eraser", OLED_8X16);
    OLED_ShowString(20, 32, "Loading...", OLED_8X16);
    OLED_Update();
    delay_ms(1000);

    /* ==================== 主循环 ==================== */
    while (1)
    {
        /* ---- 1. 按键扫描 ---- */
        key = Key_Scan();

        /* 返回键兜底: 当事件队列未取到按键时，直接读取引脚并做按下沿锁存 */
        if (key == KEY_NONE)
        {
            GPIO_PinState back_state = HAL_GPIO_ReadPin(KEY_BACK_PORT, KEY_BACK_PIN);
            if (back_state == GPIO_PIN_RESET)
            {
                if (!back_latch)
                {
                    key = KEY_BACK;
                    back_latch = 1;
                }
            }
            else
            {
                back_latch = 0;
            }
        }

        /* KEY_MOTOR 全局快捷键: 无论在哪个页面都直接切换毛刷 */
        if (key == KEY_MOTOR)
        {
            Menu_ToggleBrush();
            Menu_ApplyActuators();
            key = KEY_NONE; /* 消耗掉, 不传给菜单框架 */
        }

        /* ---- 2. 菜单逻辑处理 ---- */
        MF_Process(key);

        /* ---- 3. 蓝牙命令处理 ---- */
        Menu_HandleBluetooth();

        /* ---- 4. 定时任务: 传感器采集 + 自动控制 (200ms) ---- */
        sensor_tick += 50;
        if (sensor_tick >= 200)
        {
            sensor_tick = 0;

            /* 读取灰尘传感器 */
            if (DC01_HasNewData())
            {
                DC01_ReadPMugm3(&g_pm25, &g_pm10);
            }

            /* 读取水位 */
            g_water_percent = WaterLevel_ReadPercent();

            /* 执行自动控制逻辑 */
            Menu_AutoControl();
        }

        /* ---- 5. 定时任务: 蓝牙上报 (1000ms) ---- */
        bt_tick += 50;
        if (bt_tick >= 1000)
        {
            bt_tick = 0;
            Menu_SendBluetoothData();
        }

        /* ---- 6. 执行器状态同步 ---- */
        Menu_ApplyActuators();

        /* ---- 7. 渲染菜单界面 ---- */
        MF_Render();

        /* ---- 8. 基础延时 ---- */
        delay_ms(50);
    }
}
