#include "main.h"

static void Board_Init(void)
{
    uint8_t dht11_init_ok;

    HAL_Init();
    sys_stm32_clock_init(RCC_PLL_MUL9);
    delay_init(72U);

    OLED_Init();
    Key_Init();
    Buzzer_Init();
    Actuators_Init();
    Servo_Init();
    ADC_Sensors_Init();
    HC_SR04_Init();
    HX711_Init();
    JQ8400_Init();

    dht11_init_ok = (DHT11_Init() == 0U) ? 1U : 0U;
    PetFeeder_Init(dht11_init_ok);
}

int main(void)
{
    Board_Init();

    while (1)
    {                 
	PetFeeder_Process();
    }
}
