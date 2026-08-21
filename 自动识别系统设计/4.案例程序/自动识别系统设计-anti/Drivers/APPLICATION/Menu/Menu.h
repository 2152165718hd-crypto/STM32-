#ifndef __MENU_H
#define __MENU_H

#include "stm32f1xx_hal.h"
#include ".\APPLICATION\Menu\MenuFramework.h"
#include ".\Hardware\FM225\FM225.h"
#include ".\Hardware\ESP_01S\ESP_01S.h"
#include ".\Hardware\Buzzer\Buzzer.h"
#include ".\Hardware\Relay\Relay.h"

/* ======================== API ======================== */

/**
 * @brief  初始化菜单系统（构建菜单树 + 注册回调 + MF_Start）
 *         应在所有硬件 Init 之后调用
 */
void Menu_Init(void);

/**
 * @brief  ESP 数据接收回调，需通过 ESP_RegisterCallback() 注册
 * @param  packet  收到的数据包指针
 */
void Menu_ESP_DataCallback(ESP_DataPacket_t *packet);

/**
 * @brief  设置 ESP 模块初始化结果（在 ESP_Init 之后调用）
 * @param  ok  1=成功, 0=失败
 */
void Menu_SetESPInitOK(uint8_t ok);

/**
 * @brief  菜单系统周期性处理（在主循环中调用）
 *         处理蜂鸣器报警自动关闭等定时任务
 */
void Menu_Tick(void);

#endif /* __MENU_H */
