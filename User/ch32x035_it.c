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
#include "user_config.h"    /* BOOT_TRACE_ADDR：复位现场标记 */

void NMI_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void HardFault_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
/* ========================================================================== *
 *  6. 复位现场标记（临时诊断用）
 *  ---------------------------------------------------------------------------
 *  地址 0x20004700 在 .bss 之后（_ebss = 0x20000D84）、堆顶 0x20004800 之前，
 *  本工程堆实际用不到这一段。复位不会清 SRAM，所以这块内容能说明上一次复位：
 *    [0] == BOOT_TRACE_MAGIC → SRAM 还在，上一次不是掉电（异常/看门狗/软复位）
 *    [0] != BOOT_TRACE_MAGIC → SRAM 丢了，上一次是真的掉电
 *  写入方：User/main.c（上电打印并登记）、User/ch32x035_it.c（异常现场）。
 * ========================================================================== */
#define BOOT_TRACE_ADDR      0x20004700u
#define BOOT_TRACE_MAGIC     0x424F4F54u   /* 'BOOT' */
#define BOOT_TRACE_MAGIC_IDX 0u
#define BOOT_TRACE_CAUSE_IDX 1u
#define BOOT_TRACE_EPC_IDX   2u
#define BOOT_TRACE_TVAL_IDX  3u
#define BOOT_TRACE_NMI_IDX   4u
/*********************************************************************
 * @fn      NMI_Handler
 *
 * @brief   This function handles NMI exception.
 *
 * @return  none
 */
void NMI_Handler(void)
{
    volatile uint32_t *pu32 = (volatile uint32_t *)BOOT_TRACE_ADDR;

    pu32[BOOT_TRACE_NMI_IDX]   = 1u;                    /* 留下痕迹，等看门狗复位后由 main 打印 */
    pu32[BOOT_TRACE_MAGIC_IDX] = BOOT_TRACE_MAGIC;

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

    /* 先落标记再打印：异常上下文里的 printf 有卡死的可能（UART 状态坏了就会一直等），
       标记写在 SRAM，复位后由 main 打印，保证这条现场一定拿得到。 */
    {
        volatile uint32_t *pu32 = (volatile uint32_t *)BOOT_TRACE_ADDR;

        pu32[BOOT_TRACE_CAUSE_IDX] = u32Mcause;
        pu32[BOOT_TRACE_EPC_IDX]   = u32Mepc;
        pu32[BOOT_TRACE_TVAL_IDX]  = u32Mtval;
        pu32[BOOT_TRACE_MAGIC_IDX] = BOOT_TRACE_MAGIC;
    }

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

