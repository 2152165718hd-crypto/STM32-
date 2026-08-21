#include ".\APPLICATION\Menu\Menu.h"

/* ================================================================== */
/*                         全局状态变量                                */
/* ================================================================== */

/* ---------- 传感器实时值 ---------- */
static float g_temperature = 0.0f;  /* DS18B20 温度 (℃) */
static uint16_t g_light_adc = 4095; /* 光照 ADC (0~4095, 越小光照越强) */
static uint16_t g_rain_adc = 4095;  /* 雨滴 ADC (0~4095, 越小雨越大) */
static uint16_t g_sound_adc = 3000; /* 声音 ADC (1500~3000, 越小声音越大) */

/* ---------- 阈值 (用户可见 0~100 映射值) ---------- */
static int32_t g_light_threshold = 50; /* 光照阈值，默认50 → ADC≈2048 */
static int32_t g_sound_threshold = 67; /* 声音阈值，默认67 → ADC≈2000 */

/* ---------- 空调控制 ---------- */
static uint8_t g_ac_enabled = 0;   /* 空调开关 */
static int32_t g_ac_cool_max = 25; /* 制冷上限温度 (℃) */
static int32_t g_ac_heat_min = 20; /* 制热下限温度 (℃) */
static uint8_t g_ac_cooling = 0;   /* 当前正在制冷 */
static uint8_t g_ac_heating = 0;   /* 当前正在制热 */

/* ---------- 电饭煲控制 ---------- */
static int32_t g_rc_timer_min = 30;       /* 预约时间 (分钟) */
static uint8_t g_rc_countdown_active = 0; /* 倒计时是否在运行 */
static int32_t g_rc_remaining_sec = 0;    /* 剩余秒数 */
static uint32_t g_rc_last_tick = 0;       /* 上次倒计时tick */
static uint8_t g_rc_running = 0;          /* 电饭煲当前是否工作 */

/* ---------- 窗帘/窗户状态 ---------- */
static uint8_t g_curtain_open = 0;  /* 窗帘是否打开 */
static uint8_t g_window_closed = 0; /* 窗户是否关闭 */

/* ---------- 窗帘电机定时控制 ---------- */
static uint8_t g_motor_active = 0;      /* 电机是否正在运行 */
static int8_t g_motor_direction = 0;    /* 1=正转(开帘), -1=反转(关帘) */
static uint32_t g_motor_start_tick = 0; /* 电机启动时间 */

/* ---------- 厕所灯自动控制 ---------- */
static uint8_t g_washroom_auto_on = 0;  /* 厕所灯是否被自动点亮 */
static uint32_t g_washroom_on_tick = 0; /* 自动点亮时的tick */

/* ---------- ESP 通信 ---------- */
static uint8_t g_esp_link_id = 0; /* 活跃连接ID */
static uint8_t g_esp_client_connected = 0;
static uint32_t g_esp_last_upload = 0; /* 上次上报tick */
static uint32_t g_temp_last_tick = 0;  /* 上次温度采样tick */

/* ESP 历史命令环形缓冲 */
static char g_esp_cmd_history[ESP_CMD_HISTORY_COUNT][ESP_CMD_MAX_LEN + 1];
static uint8_t g_esp_cmd_count = 0;     /* 已记录命令总数 (用于判断是否填满) */
static uint8_t g_esp_cmd_write_idx = 0; /* 下一个写入位置 */

/* ---------- 卧室灯远程状态跟踪(用于toggle同步) ---------- */
static uint8_t g_led1_state = 0;
static uint8_t g_led2_state = 0;
static uint8_t g_led3_state = 0;
static uint8_t g_washroom_led_state = 0;
static uint8_t g_host_led_state = 0;
static uint8_t g_cool_led_state = 0;

/* ---------- 文本缓冲 ---------- */
static char fmt_buf[64];

#define SOUND_ADC_MIN 90u
#define SOUND_ADC_MAX 3000u

/* ================================================================== */
/*                       辅助工具函数                                  */
/* ================================================================== */

/**
 * @brief  绘制水平进度条
 * @param  x, y      左上角坐标
 * @param  width     总宽度(像素)
 * @param  height    高度(像素)
 * @param  percent   百分比 0~100
 */
static void DrawProgressBar(int16_t x, int16_t y, uint8_t width, uint8_t height, uint8_t percent)
{
    if (percent > 100)
        percent = 100;

    /* 外框 */
    OLED_DrawRectangle(x, y, width, height, OLED_UNFILLED);

    /* 内部填充 */
    uint8_t fill_w = (uint8_t)(((uint16_t)(width - 4) * percent) / 100);
    if (fill_w > 0)
    {
        OLED_DrawRectangle(x + 2, y + 2, fill_w, height - 4, OLED_FILLED);
    }
}

/**
 * @brief  光照 ADC → 显示百分比 (反向: ADC=0→100%, ADC=4095→0%)
 */
static uint8_t LightADC_ToPercent(uint16_t adc)
{
    if (adc >= 4095)
        return 0;
    if (adc == 0)
        return 100;
    return (uint8_t)((4095 - adc) * 100 / 4095);
}

/**
 * @brief  雨滴 ADC → 显示百分比 (反向: ADC=0→100%, ADC=4095→0%)
 */
static uint8_t RainADC_ToPercent(uint16_t adc)
{
    if (adc >= 4095)
        return 0;
    if (adc == 0)
        return 100;
    return (uint8_t)((4095 - adc) * 100 / 4095);
}

/**
 * @brief  声音 ADC → 显示百分比 (反向, 有效范围1500~3000)
 *         ADC=1500→100%, ADC=3000→0%
 */
static uint8_t SoundADC_ToPercent(uint16_t adc)
{
    if (adc <= SOUND_ADC_MIN)
        return 0;
    if (adc >= SOUND_ADC_MAX)
        return 100;
    return (uint8_t)(((uint32_t)(adc - SOUND_ADC_MIN) * 100u) / (SOUND_ADC_MAX - SOUND_ADC_MIN));
}

/**
 * @brief  光照用户阈值(0~100) → ADC 阈值 (反向)
 *         用户值0 → ADC 4095 (最暗), 用户值100 → ADC 0 (最亮)
 */
static uint16_t LightThreshold_ToADC(int32_t user_val)
{
    if (user_val <= 0)
        return 4095;
    if (user_val >= 100)
        return 0;
    return (uint16_t)(4095 - (uint32_t)user_val * 4095 / 100);
}

/**
 * @brief  声音用户阈值(0~100) → ADC 阈值 (反向, 范围1500~3000)
 *         用户值0 → ADC 3000 (最安静), 用户值100 → ADC 1500 (最响)
 */
static uint16_t SoundThreshold_ToADC(int32_t user_val)
{
    if (user_val <= 0)
        return SOUND_ADC_MIN;
    if (user_val >= 100)
        return SOUND_ADC_MAX;
    return (uint16_t)(SOUND_ADC_MIN + (uint32_t)user_val * (SOUND_ADC_MAX - SOUND_ADC_MIN) / 100u);
}

/* ================================================================== */
/*                    自定义页面回调函数                               */
/* ================================================================== */

/* ---------- 温度传感器页面 ---------- */
static void Page_Temperature(KeyEvent_t key, uint8_t *exit_flag)
{
    if (key == KEY_BACK)
    {
        *exit_flag = 1;
        return;
    }

    /* 标题 */
    OLED_ShowString(0, 0, "Temperature", OLED_8X16);
    OLED_DrawLine(0, 16, 128, 16);

    /* 温度数值 */
    snprintf(fmt_buf, sizeof(fmt_buf), "%.1f C", g_temperature);
    OLED_ShowString(4, 20, fmt_buf, OLED_8X16);

    /* 温度进度条: 0°C~50°C 映射 */
    uint8_t temp_pct = 0;
    if (g_temperature > 0.0f)
    {
        if (g_temperature >= 50.0f)
            temp_pct = 100;
        else
            temp_pct = (uint8_t)(g_temperature * 100.0f / 50.0f);
    }
    DrawProgressBar(4, 40, 120, 12, temp_pct);

    /* 底部提示 */
    OLED_ShowString(4, 55, "B:Back", OLED_6X8);
}

/* ---------- 光照传感器页面 ---------- */
static void Page_Light(KeyEvent_t key, uint8_t *exit_flag)
{
    if (key == KEY_BACK)
    {
        *exit_flag = 1;
        return;
    }

    OLED_ShowString(0, 0, "Light Sensor", OLED_8X16);
    OLED_DrawLine(0, 16, 128, 16);

    uint8_t pct = LightADC_ToPercent(g_light_adc);
    snprintf(fmt_buf, sizeof(fmt_buf), "ADC:%u  %u%%", g_light_adc, pct);
    OLED_ShowString(4, 20, fmt_buf, OLED_8X16);

    DrawProgressBar(4, 40, 120, 12, pct);

    snprintf(fmt_buf, sizeof(fmt_buf), "Thres:%ld", (long)g_light_threshold);
    OLED_ShowString(4, 55, fmt_buf, OLED_6X8);
}

/* ---------- 雨滴传感器页面 ---------- */
static void Page_Rain(KeyEvent_t key, uint8_t *exit_flag)
{
    if (key == KEY_BACK)
    {
        *exit_flag = 1;
        return;
    }

    OLED_ShowString(0, 0, "Rain Sensor", OLED_8X16);
    OLED_DrawLine(0, 16, 128, 16);

    uint8_t pct = RainADC_ToPercent(g_rain_adc);
    snprintf(fmt_buf, sizeof(fmt_buf), "ADC:%u  %u%%", g_rain_adc, pct);
    OLED_ShowString(4, 20, fmt_buf, OLED_8X16);

    DrawProgressBar(4, 40, 120, 12, pct);

    const char *status = (g_rain_adc < RAIN_DETECT_THRESHOLD) ? "Raining!" : "Sunny";
    OLED_ShowString(4, 55, (char *)status, OLED_6X8);
}

/* ---------- 声音传感器页面 ---------- */
static void Page_Sound(KeyEvent_t key, uint8_t *exit_flag)
{
    if (key == KEY_BACK)
    {
        *exit_flag = 1;
        return;
    }

    OLED_ShowString(0, 0, "Sound Sensor", OLED_8X16);
    OLED_DrawLine(0, 16, 128, 16);

    uint8_t pct = SoundADC_ToPercent(g_sound_adc);
    snprintf(fmt_buf, sizeof(fmt_buf), "ADC:%u", g_sound_adc);
    OLED_ShowString(4, 20, fmt_buf, OLED_8X16);

    snprintf(fmt_buf, sizeof(fmt_buf), "Level:%u%%  Th:%ld%%", pct, (long)g_sound_threshold);
    OLED_ShowString(4, 38, fmt_buf, OLED_6X8);

    DrawProgressBar(4, 50, 120, 10, pct);
}

/* ---------- ESP 历史命令页面 ---------- */
static void Page_ESPHistory(KeyEvent_t key, uint8_t *exit_flag)
{
    if (key == KEY_BACK)
    {
        *exit_flag = 1;
        return;
    }

    OLED_ShowString(0, 0, "ESP Cmd Log", OLED_8X16);
    OLED_DrawLine(0, 16, 128, 16);

    uint8_t total = (g_esp_cmd_count < ESP_CMD_HISTORY_COUNT) ? g_esp_cmd_count : ESP_CMD_HISTORY_COUNT;

    if (total == 0)
    {
        OLED_ShowString(4, 28, "No commands yet", OLED_8X16);
    }
    else
    {
        /* 从最新到最旧显示，最多3条 */
        for (uint8_t i = 0; i < total; i++)
        {
            /* 计算索引: 最新的在 write_idx-1, 次新 write_idx-2 ... */
            int8_t idx = (int8_t)g_esp_cmd_write_idx - 1 - (int8_t)i;
            if (idx < 0)
                idx += ESP_CMD_HISTORY_COUNT;

            int16_t y = 18 + i * 12;
            snprintf(fmt_buf, sizeof(fmt_buf), "%d:%s", i + 1, g_esp_cmd_history[idx]);
            OLED_ShowString(4, y, fmt_buf, OLED_6X8);
        }
    }
}

/* ---------- 电饭煲倒计时显示页面 ---------- */
static void Page_RCCountdown(KeyEvent_t key, uint8_t *exit_flag)
{
    if (key == KEY_BACK)
    {
        *exit_flag = 1;
        return;
    }

    OLED_ShowString(0, 0, "Rice Cooker", OLED_8X16);
    OLED_DrawLine(0, 16, 128, 16);

    if (g_rc_running)
    {
        OLED_ShowString(4, 20, "Status: Working", OLED_8X16);
        OLED_ShowString(4, 40, "Cooker is ON", OLED_8X16);
    }
    else if (g_rc_countdown_active)
    {
        int32_t min = g_rc_remaining_sec / 60;
        int32_t sec = g_rc_remaining_sec % 60;
        snprintf(fmt_buf, sizeof(fmt_buf), "Remain: %ld:%02ld", (long)min, (long)sec);
        OLED_ShowString(4, 20, fmt_buf, OLED_8X16);

        /* 倒计时进度条 */
        uint8_t pct = 0;
        int32_t total = g_rc_timer_min * 60;
        if (total > 0)
            pct = (uint8_t)((g_rc_remaining_sec * 100) / total);
        DrawProgressBar(4, 40, 120, 12, pct);
    }
    else
    {
        OLED_ShowString(4, 20, "Status: Idle", OLED_8X16);
        snprintf(fmt_buf, sizeof(fmt_buf), "Set: %ldmin", (long)g_rc_timer_min);
        OLED_ShowString(4, 40, fmt_buf, OLED_8X16);
    }

    OLED_ShowString(4, 55, "B:Back", OLED_6X8);
}

/* ---------- 窗帘/窗户状态页面 ---------- */
static void Page_CurtainWindow(KeyEvent_t key, uint8_t *exit_flag)
{
    if (key == KEY_BACK)
    {
        *exit_flag = 1;
        return;
    }

    OLED_ShowString(0, 0, "CurtainWindow", OLED_8X16);
    OLED_DrawLine(0, 16, 128, 16);

    /* 窗帘状态 */
    snprintf(fmt_buf, sizeof(fmt_buf), "Curtain: %s", g_curtain_open ? "Open" : "Closed");
    OLED_ShowString(4, 20, fmt_buf, OLED_8X16);

    /* 窗户状态 */
    snprintf(fmt_buf, sizeof(fmt_buf), "Window:  %s", g_window_closed ? "Closed" : "Open");
    OLED_ShowString(4, 38, fmt_buf, OLED_8X16);

    OLED_ShowString(4, 55, "B:Back", OLED_6X8);
}

/* ================================================================== */
/*                       菜单操作回调                                  */
/* ================================================================== */

/* --- 启动电机定时运行 (非阻塞) --- */
static void Motor_StartTimed(int8_t direction)
{
    int8_t speed = (direction > 0) ? 80 : -80;
    Motor_SetLeftSpeed(speed);
    Motor_SetRightSpeed(speed);
    g_motor_active = 1;
    g_motor_direction = direction;
    g_motor_start_tick = HAL_GetTick();
}

/* --- 空调开关变更回调 --- */
static void AC_SetManualOn(void)
{
    g_ac_enabled = 1;
    g_ac_cooling = 0;
    g_ac_heating = 0;
    Relay_SetState(RELAY_ID_AC, 1);
}

static void AC_SetOff(void)
{
    g_ac_enabled = 0;
    Relay_SetState(RELAY_ID_AC, 0);
    LED_Off(HOST_LED_GPIO_PORT, HOST_LED_PIN);
    LED_Off(COOL_LED_GPIO_PORT, COOL_LED_PIN);
    g_ac_cooling = 0;
    g_ac_heating = 0;
}

static void OnACToggle(void)
{
    if (g_ac_enabled)
    {
        /* 开启空调时，清除一下冷却、制热状态，让主循环BusinessLogic能立即重新评估并启动继电器和指示灯 */
        g_ac_cooling = 0;
        g_ac_heating = 0;
        /* 不要在这里直接打开继电器，应该由 BusinessLogic 判断温度后决定 */
        Relay_SetState(RELAY_ID_AC, 1);
    }
    else
    {
        /* 关闭空调时同时关继电器和指示灯 */
        Relay_SetState(RELAY_ID_AC, 0);
        LED_Off(HOST_LED_GPIO_PORT, HOST_LED_PIN);
        LED_Off(COOL_LED_GPIO_PORT, COOL_LED_PIN);
        g_ac_cooling = 0;
        g_ac_heating = 0;
    }
}

/* --- 电饭煲开始预约 --- */
static void OnRCStartCountdown(void)
{
    if (g_rc_timer_min <= 0)
        return;
    if (g_rc_countdown_active || g_rc_running)
        return;

    g_rc_countdown_active = 1;
    g_rc_remaining_sec = g_rc_timer_min * 60;
    g_rc_last_tick = HAL_GetTick();
}

/* --- 电饭煲立即启动 --- */
static void OnRCStartNow(void)
{
    g_rc_countdown_active = 0;
    g_rc_remaining_sec = 0;
    g_rc_running = 1;
    Relay_SetState(RELAY_ID_RC, 1);
}

/* --- 电饭煲停止 --- */
static void OnRCStop(void)
{
    g_rc_countdown_active = 0;
    g_rc_remaining_sec = 0;
    g_rc_running = 0;
    Relay_SetState(RELAY_ID_RC, 0);
}

/* --- LED 灯控制回调 --- */
static void OnLEDRoom1Toggle(void)
{
    if (g_led1_state)
        LED_On(ROOM1_LED_GPIO_PORT, ROOM1_LED_PIN);
    else
        LED_Off(ROOM1_LED_GPIO_PORT, ROOM1_LED_PIN);
}

static void OnLEDRoom2Toggle(void)
{
    if (g_led2_state)
        LED_On(ROOM2_LED_GPIO_PORT, ROOM2_LED_PIN);
    else
        LED_Off(ROOM2_LED_GPIO_PORT, ROOM2_LED_PIN);
}

static void OnLEDRoom3Toggle(void)
{
    if (g_led3_state)
        LED_On(ROOM3_LED_GPIO_PORT, ROOM3_LED_PIN);
    else
        LED_Off(ROOM3_LED_GPIO_PORT, ROOM3_LED_PIN);
}

static void OnLEDWashroomToggle(void)
{
    if (g_washroom_led_state)
    {
        LED_On(WASHROOM_LED_GPIO_PORT, WASHROOM_LED_PIN);
        g_washroom_auto_on = 0; /* 手动控制取消自动模式 */
    }
    else
    {
        LED_Off(WASHROOM_LED_GPIO_PORT, WASHROOM_LED_PIN);
        g_washroom_auto_on = 0;
    }
}

/* --- 窗帘手动控制回调 --- */
static void OnCurtainOpen(void)
{
    if (!g_curtain_open && !g_motor_active)
    {
        Motor_StartTimed(1); /* 正转开帘 */
        g_curtain_open = 1;
    }
}

static void OnCurtainClose(void)
{
    if (g_curtain_open && !g_motor_active)
    {
        Motor_StartTimed(-1); /* 反转关帘 */
        g_curtain_open = 0;
    }
}

/* --- 窗户手动控制回调 --- */
static void OnWindowOpen(void)
{
    Servo_SetAngle(WINDOW_OPEN_ANGLE);
    g_window_closed = 0;
}

static void OnWindowClose(void)
{
    Servo_SetAngle(WINDOW_CLOSE_ANGLE);
    g_window_closed = 1;
}

/* --- 光标样式切换回调 --- */
static uint8_t g_cursor_box_mode = 0; /* 0=反色, 1=方框 */
static void OnCursorToggle(void)
{
    if (g_cursor_box_mode)
        MF_SetCursorStyle(MF_CURSOR_BOX);
    else
        MF_SetCursorStyle(MF_CURSOR_REVERSE);
}

/* ================================================================== */
/*                       菜单树构建                                    */
/* ================================================================== */

void Menu_Init(void)
{
    /* ---- 创建所有菜单页 ---- */
    MF_Menu_t *menu_main = MF_CreateMenu("Home Control");
    MF_Menu_t *menu_sensors = MF_CreateMenu("Sensor Status");
    MF_Menu_t *menu_thresholds = MF_CreateMenu("Threshold Set");
    MF_Menu_t *menu_ac = MF_CreateMenu("AC Control");
    MF_Menu_t *menu_rc = MF_CreateMenu("Rice Cooker");
    MF_Menu_t *menu_led = MF_CreateMenu("LED Control");
    MF_Menu_t *menu_cw = MF_CreateMenu("Curtain/Window");

    /* ---- 传感器状态子菜单 ---- */
    MF_AddCustomPage(menu_sensors, "Temperature", Page_Temperature);
    MF_AddCustomPage(menu_sensors, "Light Sensor", Page_Light);
    MF_AddCustomPage(menu_sensors, "Rain Sensor", Page_Rain);
    MF_AddCustomPage(menu_sensors, "Sound Sensor", Page_Sound);

    /* ---- 阈值设置子菜单 ---- */
    MF_AddValue(menu_thresholds, "Light Thres", &g_light_threshold,
                0, 100, 5, "%", NULL);
    MF_AddValue(menu_thresholds, "Sound Thres", &g_sound_threshold,
                0, 100, 5, "%", NULL);

    /* ---- 空调控制子菜单 ---- */
    MF_AddToggle(menu_ac, "AC Switch", &g_ac_enabled, OnACToggle);
    MF_AddValue(menu_ac, "Cool Max", &g_ac_cool_max,
                20, 40, 1, "C", NULL);
    MF_AddValue(menu_ac, "Heat Min", &g_ac_heat_min,
                5, 25, 1, "C", NULL);

    /* ---- 电饭煲子菜单 ---- */
    MF_AddValue(menu_rc, "Timer", &g_rc_timer_min,
                0, 180, 5, "min", NULL);
    MF_AddAction(menu_rc, "Start Timer", OnRCStartCountdown);
    MF_AddAction(menu_rc, "Start Now", OnRCStartNow);
    MF_AddAction(menu_rc, "Stop", OnRCStop);
    MF_AddCustomPage(menu_rc, "Status", Page_RCCountdown);

    /* ---- LED 灯控制子菜单 ---- */
    MF_AddToggle(menu_led, "Room1 LED", &g_led1_state, OnLEDRoom1Toggle);
    MF_AddToggle(menu_led, "Room2 LED", &g_led2_state, OnLEDRoom2Toggle);
    MF_AddToggle(menu_led, "Room3 LED", &g_led3_state, OnLEDRoom3Toggle);
    MF_AddToggle(menu_led, "Washroom LED", &g_washroom_led_state, OnLEDWashroomToggle);

    /* ---- 窗帘/窗户控制子菜单 ---- */
    MF_AddCustomPage(menu_cw, "Status", Page_CurtainWindow);
    MF_AddAction(menu_cw, "Open Curtain", OnCurtainOpen);
    MF_AddAction(menu_cw, "Close Curtain", OnCurtainClose);
    MF_AddAction(menu_cw, "Open Window", OnWindowOpen);
    MF_AddAction(menu_cw, "Close Window", OnWindowClose);

    /* ---- 主菜单 ---- */
    MF_AddSubmenu(menu_main, "Sensors", menu_sensors);
    MF_AddSubmenu(menu_main, "Thresholds", menu_thresholds);
    MF_AddSubmenu(menu_main, "AC Control", menu_ac);
    MF_AddSubmenu(menu_main, "Rice Cooker", menu_rc);
    MF_AddSubmenu(menu_main, "LED Control", menu_led);
    MF_AddSubmenu(menu_main, "Curtain/Win", menu_cw);
    MF_AddCustomPage(menu_main, "ESP Cmd Log", Page_ESPHistory);
    MF_AddToggle(menu_main, "Cursor:Box", &g_cursor_box_mode, OnCursorToggle);

    /* ---- 启动菜单系统 ---- */
    MF_Start(menu_main);

    /* 初始化 ESP 历史命令缓冲 */
    memset(g_esp_cmd_history, 0, sizeof(g_esp_cmd_history));
}

/* ================================================================== */
/*                     业务逻辑处理                                    */
/* ================================================================== */

void Menu_BusinessLogic(void)
{
    uint32_t now = HAL_GetTick();

    /* ======== 1. 采集传感器数据 ======== */
    g_light_adc = LightSensor_ReadAnalog();
    g_sound_adc = SoundSensor_ReadAnalog();
    g_rain_adc = RainSensor_ReadAnalog();

    /* DS18B20 定时采样 (阻塞约750ms, 不能每次循环都读) */
    if (now - g_temp_last_tick >= DS18B20_SAMPLE_INTERVAL_MS)
    {
        g_temp_last_tick = now;
        float temp = DS18B20_ReadTemperature();
        /* 温度读取失败保护 */
        if (temp > DS18B20_TEMP_ERROR + 1.0f)
            g_temperature = temp;
    }

    /* ======== 2. 空调自动控制 (温度超限自动开启) ======== */
    if (g_temperature > (float)g_ac_cool_max)
    {
        /* 温度高于制冷上限 → 自动开启空调，制冷 */
        if (!g_ac_cooling)
        {
            g_ac_enabled = 1;
            Relay_SetState(RELAY_ID_AC, 1);
            LED_On(COOL_LED_GPIO_PORT, COOL_LED_PIN);  /* 制冷指示灯 */
            LED_Off(HOST_LED_GPIO_PORT, HOST_LED_PIN); /* 关制热灯 */
            g_ac_cooling = 1;
            g_ac_heating = 0;
        }
    }
    else if (g_temperature < (float)g_ac_heat_min)
    {
        /* 温度低于制热下限 → 自动开启空调，制热 */
        if (!g_ac_heating)
        {
            g_ac_enabled = 1;
            Relay_SetState(RELAY_ID_AC, 1);
            LED_On(HOST_LED_GPIO_PORT, HOST_LED_PIN);  /* 制热指示灯 */
            LED_Off(COOL_LED_GPIO_PORT, COOL_LED_PIN); /* 关制冷灯 */
            g_ac_heating = 1;
            g_ac_cooling = 0;
        }
    }
    else
    {
        /* 温度在舒适区间 → 自动关闭空调 */
        if (g_ac_cooling || g_ac_heating)
        {
            g_ac_enabled = 0;
            Relay_SetState(RELAY_ID_AC, 0);
            LED_Off(COOL_LED_GPIO_PORT, COOL_LED_PIN);
            LED_Off(HOST_LED_GPIO_PORT, HOST_LED_PIN);
            g_ac_cooling = 0;
            g_ac_heating = 0;
        }
    }

    /* ======== 3. 窗帘电机定时停止 ======== */
    if (g_motor_active)
    {
        if (now - g_motor_start_tick >= CURTAIN_MOTOR_DURATION_MS)
        {
            Motor_SetLeftSpeed(0);
            Motor_SetRightSpeed(0);
            g_motor_active = 0;
        }
    }

    /* ======== 4. 窗帘自动控制 (光照不足时开帘, 不自动关帘) ======== */
    {
        uint16_t light_adc_threshold = LightThreshold_ToADC(g_light_threshold);

        if (g_light_adc > light_adc_threshold)
        {
            /* 光照不足 → 自动开帘 (仅当窗帘关闭且电机空闲时) */
            if (!g_curtain_open && !g_motor_active)
            {
                Motor_StartTimed(1); /* 正转开帘 */
                g_curtain_open = 1;
            }
        }
        /* 不自动关帘，需要通过菜单或ESP手动关闭 */
    }

    /* ======== 5. 窗户自动控制 (下雨关窗, 不自动开窗) ======== */
    if (g_rain_adc < RAIN_DETECT_THRESHOLD)
    {
        /* 检测到下雨 → 自动关窗 */
        if (!g_window_closed)
        {
            Servo_SetAngle(WINDOW_CLOSE_ANGLE);
            g_window_closed = 1;
        }
    }
    /* 不自动开窗，需要通过菜单或ESP手动打开 */

    /* ======== 6. 厕所灯自动控制 (声音触发 + 光线暗) ======== */
    {
        uint16_t sound_adc_threshold = SoundThreshold_ToADC(g_sound_threshold);
        uint16_t light_adc_threshold = LightThreshold_ToADC(g_light_threshold);

        if (!g_washroom_auto_on)
        {
            /* 声音超标 且 光线不足 → 开灯 */
            if (g_sound_adc > sound_adc_threshold && g_light_adc > light_adc_threshold)
            {
                LED_On(WASHROOM_LED_GPIO_PORT, WASHROOM_LED_PIN);
                g_washroom_auto_on = 1;
                g_washroom_led_state = 1;
                g_washroom_on_tick = now;
            }
        }
        else
        {
            /* 已亮灯，检查是否超时 */
            if (now - g_washroom_on_tick >= WASHROOM_LIGHT_DURATION_MS)
            {
                LED_Off(WASHROOM_LED_GPIO_PORT, WASHROOM_LED_PIN);
                g_washroom_auto_on = 0;
                g_washroom_led_state = 0;
            }
        }
    }

    /* ======== 7. 电饭煲倒计时 ======== */
    if (g_rc_countdown_active)
    {
        if (now - g_rc_last_tick >= RC_COUNTDOWN_INTERVAL_MS)
        {
            g_rc_last_tick = now;
            g_rc_remaining_sec--;

            if (g_rc_remaining_sec <= 0)
            {
                /* 倒计时结束 → 启动电饭煲 */
                g_rc_countdown_active = 0;
                g_rc_remaining_sec = 0;
                g_rc_running = 1;
                Relay_SetState(RELAY_ID_RC, 1);
            }
        }
    }

    /* ======== 8. ESP 数据上报 ======== */
    if (g_esp_client_connected && (now - g_esp_last_upload >= ESP_UPLOAD_INTERVAL_MS))
    {
        char tx_buf[128];
        snprintf(tx_buf, sizeof(tx_buf),
                 "Temp:%.1f,Light:%u,Sound:%u,Rain:%u\r\n",
                 g_temperature, g_light_adc, g_sound_adc, g_rain_adc);

        if (ESP_SendString(g_esp_link_id, tx_buf) != ESP_OK)
        {
            g_esp_client_connected = 0;
        }
        g_esp_last_upload = now;
    }
}

/* ================================================================== */
/*                    ESP 数据接收处理                                  */
/* ================================================================== */

void Menu_OnESPData(ESP_DataPacket_t *packet)
{
    if (packet == NULL || packet->len == 0)
        return;

    /* 提取字符串 */
    char data_str[ESP_DATA_BUF_SIZE + 1];
    uint16_t copy_len = packet->len;
    if (copy_len > ESP_DATA_BUF_SIZE)
        copy_len = ESP_DATA_BUF_SIZE;
    memcpy(data_str, packet->data, copy_len);
    data_str[copy_len] = '\0';

    /* 去掉末尾换行 */
    while (copy_len > 0 && (data_str[copy_len - 1] == '\r' || data_str[copy_len - 1] == '\n'))
    {
        data_str[--copy_len] = '\0';
    }

    /* 记录活跃连接 */
    g_esp_link_id = packet->link_id;
    g_esp_client_connected = 1;

    /* 存入历史命令环形缓冲 */
    strncpy(g_esp_cmd_history[g_esp_cmd_write_idx], data_str, ESP_CMD_MAX_LEN);
    g_esp_cmd_history[g_esp_cmd_write_idx][ESP_CMD_MAX_LEN] = '\0';
    g_esp_cmd_write_idx = (g_esp_cmd_write_idx + 1) % ESP_CMD_HISTORY_COUNT;
    if (g_esp_cmd_count < ESP_CMD_HISTORY_COUNT)
        g_esp_cmd_count++;

    /* Echo 回显 */
    {
        char echo_buf[64];
        snprintf(echo_buf, sizeof(echo_buf), "OK:%s\r\n", data_str);
        ESP_SendString(packet->link_id, echo_buf);
    }

    /* ======== 远程控制命令解析 ======== */

    /* LED1 ~ LED3 : 卧室灯 */
    if (strcmp(data_str, "LED1_ON") == 0)
    {
        LED_On(ROOM1_LED_GPIO_PORT, ROOM1_LED_PIN);
        g_led1_state = 1;
    }
    else if (strcmp(data_str, "LED1_OFF") == 0)
    {
        LED_Off(ROOM1_LED_GPIO_PORT, ROOM1_LED_PIN);
        g_led1_state = 0;
    }
    else if (strcmp(data_str, "LED2_ON") == 0)
    {
        LED_On(ROOM2_LED_GPIO_PORT, ROOM2_LED_PIN);
        g_led2_state = 1;
    }
    else if (strcmp(data_str, "LED2_OFF") == 0)
    {
        LED_Off(ROOM2_LED_GPIO_PORT, ROOM2_LED_PIN);
        g_led2_state = 0;
    }
    else if (strcmp(data_str, "LED3_ON") == 0)
    {
        LED_On(ROOM3_LED_GPIO_PORT, ROOM3_LED_PIN);
        g_led3_state = 1;
    }
    else if (strcmp(data_str, "LED3_OFF") == 0)
    {
        LED_Off(ROOM3_LED_GPIO_PORT, ROOM3_LED_PIN);
        g_led3_state = 0;
    }
    /* LED4 : 卫生间灯 */
    else if (strcmp(data_str, "LED4_ON") == 0)
    {
        LED_On(WASHROOM_LED_GPIO_PORT, WASHROOM_LED_PIN);
        g_washroom_led_state = 1;
        g_washroom_auto_on = 0; /* 远程控制时取消自动模式 */
    }
    else if (strcmp(data_str, "LED4_OFF") == 0)
    {
        LED_Off(WASHROOM_LED_GPIO_PORT, WASHROOM_LED_PIN);
        g_washroom_led_state = 0;
        g_washroom_auto_on = 0;
    }
    /* LED5 : 制热指示灯 (远程手动控制) */
    else if (strcmp(data_str, "LED5_ON") == 0)
    {
        LED_On(HOST_LED_GPIO_PORT, HOST_LED_PIN);
        g_host_led_state = 1;
    }
    else if (strcmp(data_str, "LED5_OFF") == 0)
    {
        LED_Off(HOST_LED_GPIO_PORT, HOST_LED_PIN);
        g_host_led_state = 0;
    }
    /* LED6 : 制冷指示灯 (远程手动控制) */
    else if (strcmp(data_str, "LED6_ON") == 0)
    {
        LED_On(COOL_LED_GPIO_PORT, COOL_LED_PIN);
        g_cool_led_state = 1;
    }
    else if (strcmp(data_str, "LED6_OFF") == 0)
    {
        LED_Off(COOL_LED_GPIO_PORT, COOL_LED_PIN);
        g_cool_led_state = 0;
    }
    /* 空调继电器 */
    else if (strcmp(data_str, "AC_ON") == 0)
    {
        AC_SetManualOn();
    }
    else if (strcmp(data_str, "AC_OFF") == 0)
    {
        AC_SetOff();
    }
    /* 电饭煲 */
    else if (strcmp(data_str, "RC_ON") == 0)
    {
        OnRCStartNow();
    }
    else if (strcmp(data_str, "RC_OFF") == 0)
    {
        OnRCStop();
    }
    /* 窗帘 (电机, 定时运行) */
    else if (strcmp(data_str, "CURTAIN_OPEN") == 0)
    {
        if (!g_curtain_open && !g_motor_active)
        {
            Motor_StartTimed(1);
            g_curtain_open = 1;
        }
    }
    else if (strcmp(data_str, "CURTAIN_CLOSE") == 0)
    {
        if (g_curtain_open && !g_motor_active)
        {
            Motor_StartTimed(-1);
            g_curtain_open = 0;
        }
    }
    /* 窗户 (舵机) */
    else if (strcmp(data_str, "WINDOW_OPEN") == 0)
    {
        Servo_SetAngle(WINDOW_OPEN_ANGLE);
        g_window_closed = 0;
    }
    else if (strcmp(data_str, "WINDOW_CLOSE") == 0)
    {
        Servo_SetAngle(WINDOW_CLOSE_ANGLE);
        g_window_closed = 1;
    }
    /* 舵机角度直接设置: SERVO_xxx */
    else if (strncmp(data_str, "SERVO_", 6) == 0 ||
             strncmp(data_str, "SERVO:", 6) == 0 ||
             strncmp(data_str, "SERVO=", 6) == 0)
    {
        long val = strtol(data_str + 6, NULL, 10);
        if (val < 0)
            val = 0;
        if (val > 180)
            val = 180;
        Servo_SetAngle((uint8_t)val);
    }
    /* 电机速度直接设置: MOTOR:L,R */
    else if (strncmp(data_str, "MOTOR:", 6) == 0 ||
             strncmp(data_str, "MOTOR_", 6) == 0 ||
             strncmp(data_str, "MOTOR=", 6) == 0)
    {
        char *end_ptr;
        long left_val = strtol(data_str + 6, &end_ptr, 10);
        if (*end_ptr == ',')
        {
            long right_val = strtol(end_ptr + 1, NULL, 10);
            if (left_val < -100)
                left_val = -100;
            if (left_val > 100)
                left_val = 100;
            if (right_val < -100)
                right_val = -100;
            if (right_val > 100)
                right_val = 100;
            Motor_SetLeftSpeed((int8_t)left_val);
            Motor_SetRightSpeed((int8_t)right_val);
        }
    }
}
