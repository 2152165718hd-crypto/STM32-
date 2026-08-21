#include "HARDWARE/Relay/Relay.h"

typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pin;
} RelayPin_t;

static const RelayPin_t s_relayPins[RELAY_COUNT] = {
    {GPIOD, GPIO_PIN_14}, /* Relay 1 */
    {GPIOD, GPIO_PIN_15}, /* Relay 2 */
    {GPIOD, GPIO_PIN_12}, /* Relay 3 */
    {GPIOD, GPIO_PIN_13}, /* Relay 4 */
    {GPIOD, GPIO_PIN_10}, /* Relay 5 */
    {GPIOD, GPIO_PIN_11}, /* Relay 6 */
    {GPIOD, GPIO_PIN_8},  /* Relay 7 */
    {GPIOD, GPIO_PIN_9},  /* Relay 8 */
    {GPIOE, GPIO_PIN_13}, /* Relay 9 */
    {GPIOE, GPIO_PIN_11}, /* Relay 10 */
    {GPIOE, GPIO_PIN_9},  /* Relay 11 */
    {GPIOE, GPIO_PIN_7},  /* Relay 12 */
    {GPIOB, GPIO_PIN_0},  /* Relay 13 */
    {GPIOC, GPIO_PIN_4},  /* Relay 14 */
    {GPIOA, GPIO_PIN_6},  /* Relay 15 */
    {GPIOA, GPIO_PIN_4},  /* Relay 16 */
};

static GPIO_PinState Relay_GetInactiveLevel(void)
{
    return (RELAY_ACTIVE_LEVEL == GPIO_PIN_SET) ? GPIO_PIN_RESET : GPIO_PIN_SET;
}

static void Relay_EnablePortClock(GPIO_TypeDef *port)
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
    else if (port == GPIOE)
    {
        __HAL_RCC_GPIOE_CLK_ENABLE();
    }
}

void Relay_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    uint8_t i;

    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

    for (i = 0U; i < RELAY_COUNT; i++)
    {
        Relay_EnablePortClock(s_relayPins[i].port);
        GPIO_InitStruct.Pin = s_relayPins[i].pin;
        HAL_GPIO_Init(s_relayPins[i].port, &GPIO_InitStruct);
    }

    Relay_AllOff();
}

void Relay_SetState(uint8_t relay_id, uint8_t state)
{
    GPIO_PinState level;

    if ((relay_id == 0U) || (relay_id > RELAY_COUNT))
    {
        return;
    }

    level = (state != 0U) ? RELAY_ACTIVE_LEVEL : Relay_GetInactiveLevel();
    HAL_GPIO_WritePin(s_relayPins[relay_id - 1U].port, s_relayPins[relay_id - 1U].pin, level);
}

void Relay_AllOff(void)
{
    uint8_t i;

    for (i = 1U; i <= RELAY_COUNT; i++)
    {
        Relay_SetState(i, 0U);
    }
}
