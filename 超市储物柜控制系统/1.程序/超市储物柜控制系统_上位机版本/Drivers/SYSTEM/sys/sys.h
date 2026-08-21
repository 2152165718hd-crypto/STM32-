#ifndef _SYS_H
#define _SYS_H

#include "stm32f4xx.h"
#include "core_cm4.h"
#include "stm32f4xx_hal.h"


/**
 * SYS_SUPPORT_OS：系统是否支持操作系统
 * 0:不支持; 1:支持
 */
#define SYS_SUPPORT_OS         0


void sys_nvic_set_vector_table(uint32_t baseaddr, uint32_t offset);         /* 设置中断向量表偏移 */
void sys_standby(void);                                                     /* 进入待机模式 */
void sys_soft_reset(void);                                                  /* 系统软复位 */
uint8_t sys_stm32_clock_init(uint32_t plln, uint32_t pllm, uint32_t pllp, uint32_t pllq); /* 初始化系统时钟 */


/* 以下为底层函数 */
void sys_wfi_set(void);             /* 执行WFI指令 */
void sys_intx_disable(void);        /* 关闭总中断 */
void sys_intx_enable(void);         /* 开启总中断 */
void sys_msr_msp(uint32_t addr);    /* 设置栈顶地址 */

#endif
