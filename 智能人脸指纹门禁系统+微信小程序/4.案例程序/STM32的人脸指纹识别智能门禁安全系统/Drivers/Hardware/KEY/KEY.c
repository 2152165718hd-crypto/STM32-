#include ".\Hardware\KEY\KEY.h"
#include <stdint.h>

/* 非阻塞消抖阈值（毫秒） */
#define KEY_DEBOUNCE_MS 20

typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pin;
    uint8_t stable;    // 稳定状态：0 未按下，1 按下
    uint8_t last;      // 上一次读取到的原始状态
    uint32_t lastTick; // 上次状态变化时的节拍
} KeyState_t;

static KeyState_t keyStates[4] = {
    {KEY_UP_PORT, KEY_UP_PIN, 0, 0, 0},
    {KEY_DOWN_PORT, KEY_DOWN_PIN, 0, 0, 0},
    {KEY_ENTER_PORT, KEY_ENTER_PIN, 0, 0, 0},
    {KEY_BACK_PORT, KEY_BACK_PIN, 0, 0, 0},
};

void Key_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP; // 上拉，按下为低电平
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

    GPIO_InitStruct.Pin = KEY_UP_PIN;
    HAL_GPIO_Init(KEY_UP_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = KEY_DOWN_PIN;
    HAL_GPIO_Init(KEY_DOWN_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = KEY_ENTER_PIN;
    HAL_GPIO_Init(KEY_ENTER_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = KEY_BACK_PIN;
    HAL_GPIO_Init(KEY_BACK_PORT, &GPIO_InitStruct);

    /* 初始化按键状态，避免上电抖动误判 */
    uint32_t now = HAL_GetTick();
    for (int i = 0; i < 4; i++)
    {
        uint8_t raw = (HAL_GPIO_ReadPin(keyStates[i].port, keyStates[i].pin) == GPIO_PIN_RESET) ? 1 : 0;
        keyStates[i].last = raw;
        keyStates[i].stable = raw;
        keyStates[i].lastTick = now;
    }
}

/**
 * @brief  非阻塞按键扫描（定时消抖），调用频率建议 >=10ms
 * @retval 按键事件（检测到按下时返回对应事件，否则返回 KEY_NONE）
 */
KeyEvent_t Key_Scan(void)
{
    KeyEvent_t key = KEY_NONE;
    GPIO_PinState rawPin;
    uint8_t raw;
    uint32_t now = HAL_GetTick();

    for (int i = 0; i < 4; i++)
    {
        rawPin = HAL_GPIO_ReadPin(keyStates[i].port, keyStates[i].pin);
        raw = (rawPin == GPIO_PIN_RESET) ? 1 : 0; // 1 表示按下

        if (raw != keyStates[i].last)
        {
            // 状态发生变化，重置计时
            keyStates[i].last = raw;
            keyStates[i].lastTick = now;
        }
        else
        {
            // 状态稳定，判断是否超过消抖时间
            if ((now - keyStates[i].lastTick) >= KEY_DEBOUNCE_MS)
            {
                if (raw != keyStates[i].stable)
                {
                    keyStates[i].stable = raw;
                    if (raw == 1)
                    {
                        // 按下事件，只在由未按下->按下的边沿触发一次
                        switch (i)
                        {
                        case 0:
                            key = KEY_UP;
                            break;
                        case 1:
                            key = KEY_DOWN;
                            break;
                        case 2:
                            key = KEY_ENTER;
                            break;
                        case 3:
                            key = KEY_BACK;
                            break;
                        }
                        return key; // 立即返回第一个检测到的按键事件
                    }
                }
            }
        }
    }

    return key;
}
