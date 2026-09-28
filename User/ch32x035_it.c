/********************************** (C) COPYRIGHT *******************************
 * File Name          : ch32x035_it.c
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2024/10/28
 * Description        : Main Interrupt Service Routines.
*********************************************************************************
* Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/
#include "ch32x035_it.h"

void NMI_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void HardFault_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

/*********************************************************************
 * @fn      NMI_Handler
 *
 * @brief   This function handles NMI exception.
 *
 * @return  none
 */
void NMI_Handler(void)
{
    while (1)
  {
  }
}

/*********************************************************************
 * @fn      HardFault_Handler
 *
 * @brief   This function handles Hard Fault exception.
 *
 * @return  none
 */
void HardFault_Handler(void)
{
    uint32_t u32Mcause;
    uint32_t u32Mepc;
    uint32_t u32Mtval;
    uint32_t u32Mstatus;

    /* 必须先保存异常现场，后续函数调用可能改变部分 CSR。 */
    u32Mcause = __get_MCAUSE();
    u32Mepc = __get_MEPC();
    u32Mtval = __get_MTVAL();
    u32Mstatus = __get_MSTATUS();

    printf("\r\n*** HARD FAULT ***\r\n");
    printf("MCAUSE  = 0x%08lx\r\n", (unsigned long)u32Mcause);
    printf("MEPC    = 0x%08lx\r\n", (unsigned long)u32Mepc);
    printf("MTVAL   = 0x%08lx\r\n", (unsigned long)u32Mtval);
    printf("MSTATUS = 0x%08lx\r\n", (unsigned long)u32Mstatus);
    printf("******************\r\n");

    NVIC_SystemReset();
    while (1)
    {
    }
}

/*********************************************************************
 * @fn      SysTick 说明
 *
 * @brief   SysTick（内核 64 位时基）在本工程中专门服务 Debug/debug.c 的
 *          Delay_Us()/Delay_Ms() 阻塞延时（轮询计数标志，不使用中断）。
 *
 *          系统 1ms 时间片节拍由 TIM3 产生，其中断服务函数
 *          TIM3_IRQHandler() 实现在 Code/UserBsp/bsp_tick.c 中，
 *          覆盖了启动文件 Startup/startup_ch32x035.S 里的同名弱符号。
 */

