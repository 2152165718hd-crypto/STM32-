#include "./SYSTEM/sys/sys.h"
#include "./SYSTEM/usart/usart.h"

/* 若使用操作系统，需包含对应头文件 */
#if SYS_SUPPORT_OS
#include "os.h"
#endif

/******************************************************************************************/
/* 以下代码支持printf函数，需勾选use MicroLIB */

#if 1
#if (__ARMCC_VERSION >= 6010050)                    /* 使用AC6编译器时 */
__asm(".global __use_no_semihosting\n\t");          /* 不使用半主机模式 */
__asm(".global __ARM_use_no_argv \n\t");            /* AC6需指定main无参数 */

#else
/* 使用AC5编译器时，定义__FILE并禁用半主机模式 */
#pragma import(__use_no_semihosting)

struct __FILE
{
    int handle;
};

#endif

/* 半主机模式需要的函数实现 */
int _ttywrch(int ch)
{
    ch = ch;
    return ch;
}

void _sys_exit(int x)
{
    x = x;
}

char *_sys_command_string(char *cmd, int len)
{
    return NULL;
}

FILE __stdout;

/* 重定向fputc，使printf通过串口输出 */
int fputc(int ch, FILE *f)
{
    while ((USART1->SR & 0X40) == 0);               /* 等待发送完成 */
    USART1->DR = (uint8_t)ch;                       /* 发送字符 */
    return ch;
}
#endif
/***********************************************END*******************************************/
    
#if USART_EN_RX                                     /* 若使能接收功能 */

uint8_t g_usart_rx_buf[USART_REC_LEN];              /* 接收缓冲区 */

/* 接收状态标志
 * bit15: 接收完成标志
 * bit14: 接收到0x0d
 * bit13~0: 有效接收字节数
*/
uint16_t g_usart_rx_sta = 0;

uint8_t g_rx_buffer[RXBUFFERSIZE];                  /* HAL库使用的接收缓冲区 */

UART_HandleTypeDef g_uart1_handle;                  /* UART句柄 */


/**
 * @brief 初始化串口
 * @param baudrate: 波特率
 * @note 需确保时钟已正确配置
 * @retval 无
 */
void usart_init(uint32_t baudrate)
{
    g_uart1_handle.Instance = USART_UX;                         /* 指定串口 */
    g_uart1_handle.Init.BaudRate = baudrate;                    /* 波特率 */
    g_uart1_handle.Init.WordLength = UART_WORDLENGTH_8B;        /* 8位数据 */
    g_uart1_handle.Init.StopBits = UART_STOPBITS_1;             /* 1位停止位 */
    g_uart1_handle.Init.Parity = UART_PARITY_NONE;              /* 无校验位 */
    g_uart1_handle.Init.HwFlowCtl = UART_HWCONTROL_NONE;        /* 无硬件流控 */
    g_uart1_handle.Init.Mode = UART_MODE_TX_RX;                 /* 收发模式 */
    HAL_UART_Init(&g_uart1_handle);                             /* 初始化UART */
    
    /* 启动中断接收 */
    HAL_UART_Receive_IT(&g_uart1_handle, (uint8_t *)g_rx_buffer, RXBUFFERSIZE);
}

/**
 * @brief UART底层初始化（被HAL_UART_Init调用）
 * @param huart: UART句柄
 * @note 配置时钟、GPIO、中断
 * @retval 无
 */
void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    GPIO_InitTypeDef gpio_init_struct;
    if(huart->Instance == USART_UX)                             /* 针对指定串口初始化 */
    {
        USART_UX_CLK_ENABLE();                                  /* 使能串口时钟 */
        USART_TX_GPIO_CLK_ENABLE();                             /* 使能TX引脚时钟 */
        USART_RX_GPIO_CLK_ENABLE();                             /* 使能RX引脚时钟 */

        /* 配置TX引脚 */
        gpio_init_struct.Pin = USART_TX_GPIO_PIN;
        gpio_init_struct.Mode = GPIO_MODE_AF_PP;                /* 复用推挽输出 */
        gpio_init_struct.Pull = GPIO_PULLUP;                    /* 上拉 */
        gpio_init_struct.Speed = GPIO_SPEED_FREQ_HIGH;          /* 高速 */
        gpio_init_struct.Alternate = USART_TX_GPIO_AF;          /* 复用功能 */
        HAL_GPIO_Init(USART_TX_GPIO_PORT, &gpio_init_struct);

        /* 配置RX引脚 */
        gpio_init_struct.Pin = USART_RX_GPIO_PIN;
        gpio_init_struct.Alternate = USART_RX_GPIO_AF;
        HAL_GPIO_Init(USART_RX_GPIO_PORT, &gpio_init_struct);

#if USART_EN_RX
        HAL_NVIC_EnableIRQ(USART_UX_IRQn);                      /* 使能串口中断 */
        HAL_NVIC_SetPriority(USART_UX_IRQn, 3, 3);              /* 优先级配置 */
#endif
    }
}

/**
 * @brief 接收完成回调函数
 * @param huart: UART句柄
 * @retval 无
 */
void USART_Debug_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if(huart->Instance == USART_UX)             /* 针对指定串口 */
    {
        if((g_usart_rx_sta & 0x8000) == 0)      /* 未接收完成 */
        {
            if(g_usart_rx_sta & 0x4000)         /* 已接收到0x0d */
            {
                if(g_rx_buffer[0] != 0x0a) 
                {
                    g_usart_rx_sta = 0;         /* 接收错误，重新开始 */
                }
                else 
                {
                    g_usart_rx_sta |= 0x8000;   /* 标记接收完成 */
                }
            }
            else                                /* 未接收到0x0d */
            {
                if(g_rx_buffer[0] == 0x0d)
                {
                    g_usart_rx_sta |= 0x4000;
                }
                else
                {
                    g_usart_rx_buf[g_usart_rx_sta & 0X3FFF] = g_rx_buffer[0] ;
                    g_usart_rx_sta++;
                    if(g_usart_rx_sta > (USART_REC_LEN - 1))
                    {
                        g_usart_rx_sta = 0;     /* 缓冲区满，重新开始 */
                    }
                }
            }
        }
        
        /* 重新启动中断接收 */
        HAL_UART_Receive_IT(&g_uart1_handle, (uint8_t *)g_rx_buffer, RXBUFFERSIZE);
    }
}

/**
 * @brief 串口中断处理函数
 * @param 无
 * @retval 无
 */
#endif


