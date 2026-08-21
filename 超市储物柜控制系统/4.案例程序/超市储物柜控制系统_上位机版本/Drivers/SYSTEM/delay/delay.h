#ifndef __DELAY_H
#define __DELAY_H

#include "./SYSTEM/sys/sys.h"


void delay_init(uint16_t sysclk);           /* 初始化延时函数 */
void delay_ms(uint16_t nms);                /* 毫秒级延时 */
void delay_us(uint32_t nus);                /* 微秒级延时 */

void HAL_Delay(uint32_t Delay);         /* HAL库延时函数 */

#endif


