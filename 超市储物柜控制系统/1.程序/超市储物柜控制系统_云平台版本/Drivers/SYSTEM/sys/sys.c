#include "./SYSTEM/sys/sys.h"

/**
 * @brief 设置中断向量表偏移地址
 * @param baseaddr: 基地址
 * @param offset: 偏移量
 * @retval 无
 */
void sys_nvic_set_vector_table(uint32_t baseaddr, uint32_t offset)
{
    /* NVIC向量表偏移配置，VTOR低9位有效[8:0] */
    SCB->VTOR = baseaddr | (offset & (uint32_t)0xFFFFFE00);
}

/**
 * @brief 执行WFI指令（进入休眠状态，等待中断唤醒）
 * @param 无
 * @retval 无
 */
void sys_wfi_set(void)
{
    __ASM volatile("wfi");
}

/**
 * @brief 关闭总中断（不包含fault和NMI中断）
 * @param 无
 * @retval 无
 */
void sys_intx_disable(void)
{
    __ASM volatile("cpsid i");
}

/**
 * @brief 开启总中断
 * @param 无
 * @retval 无
 */
void sys_intx_enable(void)
{
    __ASM volatile("cpsie i");
}

/**
 * @brief 设置栈顶地址
 * @note 用于启动文件，MDK环境下实际未使用
 * @param addr: 栈顶地址
 * @retval 无
 */
void sys_msr_msp(uint32_t addr)
{
    __set_MSP(addr);    /* 设置栈顶地址 */
}

/**
 * @brief 进入待机模式
 * @param 无
 * @retval 无
 */
void sys_standby(void)
{
    __HAL_RCC_PWR_CLK_ENABLE();    /* 使能电源时钟 */
    SET_BIT(PWR->CR, PWR_CR_PDDS); /* 进入待机模式 */
}

/**
 * @brief 系统软复位
 * @param 无
 * @retval 无
 */
void sys_soft_reset(void)
{
    NVIC_SystemReset();
}

/**
 * @brief 初始化系统时钟
 * @param plln: PLL倍频系数(64~432)
 * @param pllm: PLL预分频系数(2~63)
 * @param pllp: PLL分频系数(用于系统时钟，可选2,4,6,8)
 * @param pllq: PLL分频系数(可选2~15)
 * @note 时钟计算公式：
 *       Fvco = Fs * (plln / pllm)  (VCO频率)
 *       Fsys = Fvco / pllp        (系统时钟频率)
 *       Fq = Fvco / pllq          (PLLQ输出频率)
 *       推荐配置(外部晶振8MHz)：plln=336, pllm=8, pllp=2, pllq=7
 *       对应：Fvco=336MHz, Fsys=168MHz, Fq=48MHz
 * @retval 0:成功; 1:失败
 */
uint8_t sys_stm32_clock_init(uint32_t plln, uint32_t pllm, uint32_t pllp, uint32_t pllq)
{
    HAL_StatusTypeDef ret = HAL_OK;
    RCC_OscInitTypeDef rcc_osc_init = {0};
    RCC_ClkInitTypeDef rcc_clk_init = {0};

    __HAL_RCC_PWR_CLK_ENABLE();                                         /* 使能PWR时钟 */
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);      /* 配置电源电压缩放 */

    /* 配置HSE为PLL时钟源 */
    rcc_osc_init.OscillatorType = RCC_OSCILLATORTYPE_HSE;        /* 时钟源为HSE */
    rcc_osc_init.HSEState = RCC_HSE_ON;                          /* 开启HSE */
    rcc_osc_init.PLL.PLLState = RCC_PLL_ON;                      /* 开启PLL */
    rcc_osc_init.PLL.PLLSource = RCC_PLLSOURCE_HSE;              /* PLL源选择HSE */
    rcc_osc_init.PLL.PLLN = plln;
    rcc_osc_init.PLL.PLLM = pllm;
    rcc_osc_init.PLL.PLLP = pllp;
    rcc_osc_init.PLL.PLLQ = pllq;
    ret = HAL_RCC_OscConfig(&rcc_osc_init);                      /* 初始化RCC振荡器 */
    if(ret != HAL_OK)
    {
        return 1;                                                /* 时钟初始化失败 */
    }

    /* 配置系统时钟源及分频 */
    rcc_clk_init.ClockType = ( RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2);
    rcc_clk_init.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;         /* 系统时钟源为PLL */
    rcc_clk_init.AHBCLKDivider = RCC_SYSCLK_DIV1;                /* AHB分频系数1 */
    rcc_clk_init.APB1CLKDivider = RCC_HCLK_DIV4;                 /* APB1分频系数4 */
    rcc_clk_init.APB2CLKDivider = RCC_HCLK_DIV2;                 /* APB2分频系数2 */
    ret = HAL_RCC_ClockConfig(&rcc_clk_init, FLASH_LATENCY_5);   /* 配置FLASH延迟为5WS */
    if(ret != HAL_OK)
    {
        return 1;                                                /* 时钟配置失败 */
    }
    
    /* 使能FLASH预取缓冲区（特定芯片型号） */
    if (HAL_GetREVID() == 0x1001)
    {
        __HAL_FLASH_PREFETCH_BUFFER_ENABLE();
    }
    return 0;
}


#ifdef  USE_FULL_ASSERT

/**
 * @brief 断言失败处理函数
 * @param file: 源文件路径
 * @param line: 错误行号
 * @retval 无
 */
void assert_failed(uint8_t* file, uint32_t line)
{ 
    while (1)
    {
    }
}
#endif


