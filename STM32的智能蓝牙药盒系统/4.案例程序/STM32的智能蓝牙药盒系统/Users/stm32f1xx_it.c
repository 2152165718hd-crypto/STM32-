#include "main.h"
#include "stm32f1xx_it.h"

/**
 * @file stm32f1xx_it.c
 * @brief Cortex-M3 异常处理函数实现。
 */

/**
 * @brief 不可屏蔽中断处理函数。
 */
void NMI_Handler(void)
{
}

/**
 * @brief 硬件错误异常处理函数。
 * @note 进入死循环，便于调试时定位故障。
 */
void HardFault_Handler(void)
{
    while (1)
    {
    }
}

/**
 * @brief 存储器管理异常处理函数。
 */
void MemManage_Handler(void)
{
    while (1)
    {
    }
}

/**
 * @brief 总线错误异常处理函数。
 */
void BusFault_Handler(void)
{
    while (1)
    {
    }
}

/**
 * @brief 用法错误异常处理函数。
 */
void UsageFault_Handler(void)
{
    while (1)
    {
    }
}

/**
 * @brief 系统服务调用异常处理函数。
 */
void SVC_Handler(void)
{
}

/**
 * @brief 调试监视异常处理函数。
 */
void DebugMon_Handler(void)
{
}

/**
 * @brief PendSV 异常处理函数。
 */
void PendSV_Handler(void)
{
}

/**
 * @brief SysTick 中断处理函数。
 * @note 负责维护 HAL 的系统节拍计数。
 */
void SysTick_Handler(void)
{
    HAL_IncTick();
}
