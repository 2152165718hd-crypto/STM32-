#include ".\Hardware\PH\PH.h"
#include ".\Hardware\BoardADC\BoardADC.h"

void PH_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    (void)BoardADC_Init();

    __HAL_RCC_GPIOA_CLK_ENABLE();
    gpio.Pin = PH_DO_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(PH_DO_PORT, &gpio);
}

float PH_ReadVoltage(void)
{
    return BoardADC_ReadVoltage(PH_OUT_ADC_CHANNEL);
}

float PH_ReadToVoltage(void)
{
    return BoardADC_ReadVoltage(PH_TO_ADC_CHANNEL);
}

uint8_t PH_ReadDigitalAlarm(void)
{
    return (HAL_GPIO_ReadPin(PH_DO_PORT, PH_DO_PIN) == GPIO_PIN_RESET) ? 1U : 0U;
}

PH_Data_t PH_Read(float k, float b)
{
    PH_Data_t data;

    data.voltage_v = PH_ReadVoltage();
    data.to_voltage_v = PH_ReadToVoltage();
    data.ph = (k * data.voltage_v) + b;
    data.digital_alarm = PH_ReadDigitalAlarm();

    return data;
}
