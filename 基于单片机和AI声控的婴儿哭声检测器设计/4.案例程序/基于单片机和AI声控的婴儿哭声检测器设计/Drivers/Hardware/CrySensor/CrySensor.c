#include ".\Hardware\CrySensor\CrySensor.h"

static uint8_t s_p1LastLevel = 0u;
static uint8_t s_p2LastLevel = 0u;

static void CrySensor_EnablePortClock(GPIO_TypeDef *port)
{
    if (port == GPIOA)
    {
        __HAL_RCC_GPIOA_CLK_ENABLE();
    }
    else if (port == GPIOB)
    {
        __HAL_RCC_GPIOB_CLK_ENABLE();
    }
    else if (port == GPIOC)
    {
        __HAL_RCC_GPIOC_CLK_ENABLE();
    }
    else if (port == GPIOD)
    {
        __HAL_RCC_GPIOD_CLK_ENABLE();
    }
#ifdef GPIOE
    else if (port == GPIOE)
    {
        __HAL_RCC_GPIOE_CLK_ENABLE();
    }
#endif
}

static uint8_t CrySensor_ReadPinLevel(GPIO_TypeDef *port, uint16_t pin)
{
    return (HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_SET) ? 1u : 0u;
}

void CrySensor_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    CrySensor_EnablePortClock(CRY_SENSOR_P1_GPIO_PORT);
    CrySensor_EnablePortClock(CRY_SENSOR_P2_GPIO_PORT);

    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

    GPIO_InitStruct.Pin = CRY_SENSOR_P1_PIN;
    HAL_GPIO_Init(CRY_SENSOR_P1_GPIO_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = CRY_SENSOR_P2_PIN;
    HAL_GPIO_Init(CRY_SENSOR_P2_GPIO_PORT, &GPIO_InitStruct);

    CrySensor_ResetEdgeState();
}

uint8_t CrySensor_ReadP1(void)
{
    return CrySensor_ReadPinLevel(CRY_SENSOR_P1_GPIO_PORT, CRY_SENSOR_P1_PIN);
}

uint8_t CrySensor_ReadP2(void)
{
    return CrySensor_ReadPinLevel(CRY_SENSOR_P2_GPIO_PORT, CRY_SENSOR_P2_PIN);
}

uint8_t CrySensor_P1_IsTriggered(void)
{
    return (HAL_GPIO_ReadPin(CRY_SENSOR_P1_GPIO_PORT, CRY_SENSOR_P1_PIN) == CRY_SENSOR_TRIGGER_LEVEL) ? 1u : 0u;
}

uint8_t CrySensor_P2_IsTriggered(void)
{
    return (HAL_GPIO_ReadPin(CRY_SENSOR_P2_GPIO_PORT, CRY_SENSOR_P2_PIN) == CRY_SENSOR_TRIGGER_LEVEL) ? 1u : 0u;
}

uint8_t CrySensor_GetRisingEvents(void)
{
    uint8_t events = CRY_SENSOR_EVENT_NONE;
    uint8_t p1Level = CrySensor_ReadP1();
    uint8_t p2Level = CrySensor_ReadP2();

    if ((s_p1LastLevel == 0u) && (p1Level == 1u))
    {
        events |= CRY_SENSOR_EVENT_P1_RISING;
    }

    if ((s_p2LastLevel == 0u) && (p2Level == 1u))
    {
        events |= CRY_SENSOR_EVENT_P2_RISING;
    }

    s_p1LastLevel = p1Level;
    s_p2LastLevel = p2Level;

    return events;
}

void CrySensor_ResetEdgeState(void)
{
    s_p1LastLevel = CrySensor_ReadP1();
    s_p2LastLevel = CrySensor_ReadP2();
}
