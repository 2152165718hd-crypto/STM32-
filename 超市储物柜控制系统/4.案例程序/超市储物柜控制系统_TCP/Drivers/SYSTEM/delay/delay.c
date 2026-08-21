#include "./SYSTEM/sys/sys.h"
#include "./SYSTEM/delay/delay.h"


static uint32_t g_fac_us = 0;       /* 微秒延时系数 */

/**
 * @brief 初始化延时函数
 * @param sysclk: 系统时钟频率(MHz)，如168MHz
 * @retval 无
 */  
void delay_init(uint16_t sysclk)
{
    g_fac_us = sysclk;                                  /* 初始化微秒系数 */
}

/**
 * @brief 微秒级延时
 * @param nus: 延时微秒数
 * @note 范围：0 ~ (2^32 / g_fac_us)
 * @retval 无
 */
void delay_us(uint32_t nus)
{
    uint32_t ticks;
    uint32_t told, tnow, tcnt = 0;
    uint32_t reload = SysTick->LOAD;        /* 加载值 */
    ticks = nus * g_fac_us;                 /* 计算需要的节拍数 */

    told = SysTick->VAL;                    /* 记录当前值 */
    while (1)
    {
        tnow = SysTick->VAL;
        if (tnow != told)
        {
            if (tnow < told)
            {
                tcnt += told - tnow;
            }
            else
            {
                tcnt += reload - tnow + told;
            }
            told = tnow;
            if (tcnt >= ticks) 
            {
                break;                      /* 延时结束 */
            }
        }
    }
}

/**
 * @brief 毫秒级延时
 * @param nms: 延时毫秒数
 * @retval 无
 */
void delay_ms(uint16_t nms)
{
    delay_us((uint32_t)(nms * 1000));       /* 剩余时间用微秒延时实现 */
}

/**
 * @brief HAL库延时函数重定义
 * @param Delay: 延时时长(ms)
 * @retval 无
 */
void HAL_Delay(uint32_t Delay)
{
     delay_ms(Delay);
}


