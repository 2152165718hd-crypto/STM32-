/**
  ******************************************************************************
  * @file    Templates/Src/stm32f4xx_it.c 
  * @author  MCD Application Team
  * @brief   Main Interrupt Service Routines.
  *          This file provides template for all exceptions handler and 
  *          peripherals interrupt service routine.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2017 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_it.h"
#include "stm32f4xx_hal.h"
#include "SYSTEM/usart/usart.h"
#include "HARDWARE/ESP_01S/ESP_01S.h"
#include "HARDWARE/FM225/FM225.h"
#include "HARDWARE/Independent_Key/KEY.h"
#include "HARDWARE/Matrix_Keypad/Matrix_Keypad.h"
#include "HARDWARE/TFT_ST7735/TFT_ST7735.h"
/** @addtogroup STM32F4xx_HAL_Examples
  * @{
  */

/** @addtogroup Templates
  * @{
  */

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
static volatile uint32_t s_fault_reason = 0U;
static volatile uint32_t s_fault_hfsr = 0U;
static volatile uint32_t s_fault_cfsr = 0U;
static volatile uint32_t s_fault_mmar = 0U;
static volatile uint32_t s_fault_bfar = 0U;
static volatile uint32_t s_fault_afsr = 0U;
static volatile uint32_t s_fault_icsr = 0U;

/* Private function prototypes -----------------------------------------------*/
static void App_FaultRecordAndReset(uint32_t reason);
/* Private functions ---------------------------------------------------------*/
static void App_FaultRecordAndReset(uint32_t reason)
{
  __disable_irq();

  s_fault_reason = reason;
  s_fault_hfsr = SCB->HFSR;
  s_fault_cfsr = SCB->CFSR;
  s_fault_mmar = SCB->MMFAR;
  s_fault_bfar = SCB->BFAR;
  s_fault_afsr = SCB->AFSR;
  s_fault_icsr = SCB->ICSR;

  __DSB();
  __ISB();

  if ((CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) != 0U)
  {
    __BKPT(0);
    while (1)
    {
    }
  }

  NVIC_SystemReset();
  while (1)
  {
  }
}

/******************************************************************************/
/*            Cortex-M4 Processor Exceptions Handlers                         */
/******************************************************************************/

/**
  * @brief   This function handles NMI exception.
  * @param  None
  * @retval None
  */
void NMI_Handler(void)
{
}

/**
  * @brief  This function handles Hard Fault exception.
  * @param  None
  * @retval None
  */
void HardFault_Handler(void)
{
  App_FaultRecordAndReset(0xDEAD0001U);
}

/**
  * @brief  This function handles Memory Manage exception.
  * @param  None
  * @retval None
  */
void MemManage_Handler(void)
{
  App_FaultRecordAndReset(0xDEAD0002U);
}

/**
  * @brief  This function handles Bus Fault exception.
  * @param  None
  * @retval None
  */
void BusFault_Handler(void)
{
  App_FaultRecordAndReset(0xDEAD0003U);
}

/**
  * @brief  This function handles Usage Fault exception.
  * @param  None
  * @retval None
  */
void UsageFault_Handler(void)
{
  App_FaultRecordAndReset(0xDEAD0004U);
}

/**
  * @brief  This function handles SVCall exception.
  * @param  None
  * @retval None
  */
void SVC_Handler(void)
{
}

/**
  * @brief  This function handles Debug Monitor exception.
  * @param  None
  * @retval None
  */
void DebugMon_Handler(void)
{
}

/**
  * @brief  This function handles PendSVC exception.
  * @param  None
  * @retval None
  */
void PendSV_Handler(void)
{
}

/**
  * @brief  This function handles SysTick Handler.
  * @param  None
  * @retval None
  */
void SysTick_Handler(void)
{
  HAL_IncTick();
}

/******************************************************************************/
/*                 STM32F4xx Peripherals Interrupt Handlers                   */
/*  Add here the Interrupt Handler for the used peripheral(s) (PPP), for the  */
/*  available peripheral interrupt handler's name please refer to the startup */
/*  file (startup_stm32f4xx.s).                                               */
/******************************************************************************/

/**
  * @brief  This function handles PPP interrupt request.
  * @param  None
  * @retval None
  */
/*void PPP_IRQHandler(void)
{
}*/

void USART1_IRQHandler(void)
{
  HAL_UART_IRQHandler(&g_uart1_handle);
}

void UART4_IRQHandler(void)
{
  HAL_UART_IRQHandler(ESP_GetUartHandle());
}

void UART5_IRQHandler(void)
{
  HAL_UART_IRQHandler(FM225_GetUartHandle());
}

void TIM2_IRQHandler(void)
{
  HAL_TIM_IRQHandler(Key_GetTimerHandle());
}

void DMA1_Stream4_IRQHandler(void)
{
  ST7735_SPI_DMA_IRQHandler();
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  USART_Debug_RxCpltCallback(huart);
  ESP_RxCpltCallback(huart);
  FM225_RxCallback(huart);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  ESP_ErrorCallback(huart);
  FM225_ErrorCallback(huart);
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  Key_TimerCallback(htim);
  MatrixKeypad_TimerCallback(htim);
}


/**
  * @}
  */ 

/**
  * @}
  */
