/********************************** (C) COPYRIGHT *******************************
 * File Name          : main.c
 * Brief              : pd-spoofing (CH32X035G8U6) 主程序
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/



#include "main.h"
#include "Task.h"

#include "bsp_time.h"
#include "bsp_spi.h"

/**
 * @brief  系统初始化
 * @note   顺序要求：
 *           1) 时钟与延时基础（SystemCoreClockUpdate / Delay_Init）
 *           2) 串口打印（可选，用于调试日志）
 *           3) 板级 GPIO（含 LCD 控制线与按键、输出使能的安全默认电平）
 *           4) 1ms 时间片节拍（TIM3）—— 必须在任何依赖 BspTickGetMs 的驱动之前
 *           5) SPI1 Mode 2 + DMA（LCD 输出通道）
 *           6) TIM1_CH1 + DMA（4 颗 WS2812）
 */
static void SystemInit_User(void)
{
    /* 1) 时钟与延时 */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    SystemCoreClockUpdate();
    Delay_Init();

    /* 2) 调试串口（USART1，TX=PB10，对应 LOG-TX） */
    USART_Printf_Init(115200);
    printf("\r\n[BOOT] pd-spoofing start\r\n");
    printf("[BOOT] SYSCLK=%lu Hz ChipID=%08lx\r\n",
           (unsigned long)SystemCoreClock, (unsigned long)DBGMCU_GetCHIPID());

    /* 3) 板级 GPIO：输出使能默认关断，避免上电即带载 */
    BspBoardInit();

    /* 4) 1ms 时间片节拍（TIM3） */
    BspTickInit();
    printf("[TICK] TIM3 1ms started\r\n");

    /* 5) LCD 的 SPI1 Mode 2 与 TX DMA 接口 */
    BspSpiInit();

    /* 6) WS2812 PWM 输出：PB9 = TIM1_CH1，DMA1 Channel 5 */
    BspWs2812Init();
}

/*********************************************************************
 * @fn      main
 *
 * @brief   主程序：Protothread 时间片任务调度
 *
 * @return  none
 */
int main(void)
{
    SystemInit_User();

    while (1)
    {
// #if USER_LCD_ENABLE
//         /* 显示任务：LCD 数据面板刷新 */
//         PT_TASK_REG(0, UsrDisplayTask);
// #else
//         /* LCD 已关闭：显示任务不注册，SPI1/DMA 只初始化不传输 */
// #endif

//         /* 按键任务：3 键事件扫描（单击/双击/长按） */
//         PT_TASK_REG(1, UsrButtonTask);

//         /* 系统任务：状态机推进（INIT -> POWER_ON -> RUNNING） */
//         PT_TASK_REG(2, UsrSystemTask);

//         /* 时间任务：上电时间与开机时间累计 */
//         PT_TASK_REG(3, UsrTimeTask);

//         /* WS2812 任务：4 颗灯同步颜色渐变 */
//         PT_TASK_REG(4, UsrWs2812Task);

//         /* USB-PD Sink task: CC detection and contract negotiation. */
//         PT_TASK_REG(5, UsrPdTask);
    }
}
