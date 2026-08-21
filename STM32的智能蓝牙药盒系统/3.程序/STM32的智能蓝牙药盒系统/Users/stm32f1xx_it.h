#ifndef __STM32F1XX_IT_H
#define __STM32F1XX_IT_H

/**
 * @file stm32f1xx_it.h
 * @brief Cortex-M3 异常与系统节拍中断处理函数声明。
 */

#ifdef __cplusplus
extern "C"
{
#endif

void NMI_Handler(void);
void HardFault_Handler(void);
void MemManage_Handler(void);
void BusFault_Handler(void);
void UsageFault_Handler(void);
void SVC_Handler(void);
void DebugMon_Handler(void);
void PendSV_Handler(void);
void SysTick_Handler(void);

#ifdef __cplusplus
}
#endif

#endif /* __STM32F1XX_IT_H */
