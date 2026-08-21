#include ".\Hardware\LowPower\LowPower.h"
#include ".\Hardware\Lora_L33_433UD22S\Lora_L33_433UD22S.h"

static RTC_HandleTypeDef s_hrtc;
static volatile uint8_t s_rtc_alarm_wakeup = 0U;
static uint8_t s_low_power_ready = 0U;

static void LowPower_ClockBackupDomainInit(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_PeriphCLKInitTypeDef periph = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_RCC_BKP_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();

    __HAL_RCC_BACKUPRESET_FORCE();
    __HAL_RCC_BACKUPRESET_RELEASE();

    osc.OscillatorType = RCC_OSCILLATORTYPE_LSI;
    osc.LSIState = RCC_LSI_ON;
    (void)HAL_RCC_OscConfig(&osc);

    periph.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    periph.RTCClockSelection = RCC_RTCCLKSOURCE_LSI;
    (void)HAL_RCCEx_PeriphCLKConfig(&periph);

    __HAL_RCC_RTC_ENABLE();
}

uint8_t LowPower_Init(void)
{
    RTC_TimeTypeDef time = {0};
    RTC_DateTypeDef date = {0};

    LowPower_ClockBackupDomainInit();

    s_hrtc.Instance = RTC;
    s_hrtc.Init.AsynchPrediv = RTC_AUTO_1_SECOND;
    s_hrtc.Init.OutPut = RTC_OUTPUTSOURCE_NONE;

    if (HAL_RTC_Init(&s_hrtc) != HAL_OK)
    {
        s_low_power_ready = 0U;
        return 0U;
    }

    time.Hours = 0U;
    time.Minutes = 0U;
    time.Seconds = 0U;
    (void)HAL_RTC_SetTime(&s_hrtc, &time, RTC_FORMAT_BIN);

    date.WeekDay = RTC_WEEKDAY_MONDAY;
    date.Month = RTC_MONTH_JANUARY;
    date.Date = 1U;
    date.Year = 0U;
    (void)HAL_RTC_SetDate(&s_hrtc, &date, RTC_FORMAT_BIN);

    __HAL_RTC_ALARM_EXTI_CLEAR_FLAG();
    HAL_NVIC_SetPriority(RTC_Alarm_IRQn, 1U, 1U);
    HAL_NVIC_EnableIRQ(RTC_Alarm_IRQn);

    s_low_power_ready = 1U;
    return 1U;
}

uint8_t LowPower_EnterStop(uint32_t seconds)
{
    RTC_TimeTypeDef now = {0};
    RTC_AlarmTypeDef alarm = {0};
    uint32_t now_s;
    uint32_t alarm_s;

    if ((s_low_power_ready == 0U) || (seconds == 0U))
    {
        return 0U;
    }

    if (seconds > 86399U)
    {
        seconds = 86399U;
    }

    if (HAL_RTC_GetTime(&s_hrtc, &now, RTC_FORMAT_BIN) != HAL_OK)
    {
        return 0U;
    }

    now_s = ((uint32_t)now.Hours * 3600U) + ((uint32_t)now.Minutes * 60U) + now.Seconds;
    alarm_s = (now_s + seconds) % 86400U;

    alarm.AlarmTime.Hours = (uint8_t)(alarm_s / 3600U);
    alarm.AlarmTime.Minutes = (uint8_t)((alarm_s % 3600U) / 60U);
    alarm.AlarmTime.Seconds = (uint8_t)(alarm_s % 60U);
    alarm.Alarm = RTC_ALARM_A;

    s_rtc_alarm_wakeup = 0U;
    (void)HAL_RTC_DeactivateAlarm(&s_hrtc, RTC_ALARM_A);
    if (HAL_RTC_SetAlarm_IT(&s_hrtc, &alarm, RTC_FORMAT_BIN) != HAL_OK)
    {
        return 0U;
    }

    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU);
    HAL_SuspendTick();
    /*
     * Use Sleep instead of Stop so USART3 RX can keep waking the MCU and
     * buffering incoming commands while the RTC still controls the timeout.
     */
    while ((s_rtc_alarm_wakeup == 0U) && (Lora_HasPendingLine() == 0U))
    {
        HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);
    }

    (void)HAL_RTC_DeactivateAlarm(&s_hrtc, RTC_ALARM_A);
    HAL_ResumeTick();

    return 1U;
}

uint8_t LowPower_WasRtcAlarm(void)
{
    return s_rtc_alarm_wakeup;
}

void LowPower_ClearWakeFlag(void)
{
    s_rtc_alarm_wakeup = 0U;
}

RTC_HandleTypeDef *LowPower_GetRtcHandle(void)
{
    return &s_hrtc;
}

void LowPower_RTCAlarmIRQHandler(void)
{
    HAL_RTC_AlarmIRQHandler(&s_hrtc);
}

void HAL_RTC_AlarmAEventCallback(RTC_HandleTypeDef *hrtc)
{
    if (hrtc->Instance == RTC)
    {
        s_rtc_alarm_wakeup = 1U;
    }
}
