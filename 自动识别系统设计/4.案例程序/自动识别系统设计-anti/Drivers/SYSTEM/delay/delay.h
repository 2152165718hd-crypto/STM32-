#ifndef __DELAY_H
#define __DELAY_H

#include "./SYSTEM/sys/sys.h"

void delay_init(uint16_t sysclk); /* 初始化延时函数 */
void delay_ms(uint32_t nms);      /* 延时nms */
void delay_us(uint32_t nus);      /* 延时nus */
void HAL_Delay(uint32_t Delay);   /* HAL库延时函数重定义 */

#endif
