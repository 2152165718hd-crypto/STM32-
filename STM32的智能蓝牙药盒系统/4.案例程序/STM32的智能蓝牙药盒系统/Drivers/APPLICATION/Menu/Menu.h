#ifndef __MENU_H
#define __MENU_H

/**
 * @file Menu.h
 * @brief 智能药盒应用层菜单与业务调度接口声明。
 */

/**
 * @brief 初始化菜单业务层及其依赖外设。
 * @note 该函数会完成传感器、执行器、存储、蓝牙和菜单树的整体初始化。
 */
void Menu_Init(void);

/**
 * @brief 菜单业务层周期任务。
 * @note 需要在主循环中持续调用，用于执行环境采样、提醒处理、蓝牙命令处理与界面刷新。
 */
void Menu_Task(void);

#endif /* __MENU_H */
