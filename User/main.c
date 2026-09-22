/********************************** (C) COPYRIGHT *******************************
 * File Name          : main.c
 * Brief              : pd-spoofing (CH32X035G8U6) 主程序
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/

#include "user_global.h"


/**
 * @brief  系统初始化
 */
static void System_Init(void)
{
    /* 时钟与延时 */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    SystemCoreClockUpdate();
    Delay_Init();

    USART_Printf_Init(115200);

    printf("           _                              __ _             \r\n");
    printf(" _ __   __| |      ___ _ __   ___   ___  / _(_)_ __   __ _ \r\n");
    printf("| '_ \\ / _` |_____/ __| '_ \\ / _ \\ / _ \\| |_| | '_ \\ / _` |\r\n");
    printf("| |_) | (_| |_____\\__ \\ |_) | (_) | (_) |  _| | | | | (_| |\r\n");
    printf("| .__/ \\__,_|     |___/ .__/ \\___/ \\___/|_| |_|_| |_|\\__, |\r\n");
    printf("|_|                   |_|                            |___/ \r\n");

    printf("SYSCLK=%lu Hz ChipID=%08lx\r\n",
           (unsigned long)SystemCoreClock, (unsigned long)DBGMCU_GetCHIPID());
    printf("\rby:YJRQZ777\r\n");
    
}
/**
 * @brief  初始化
 */
static void User_Init(void)
{   
    log_set_level(LOG_DEBUG);

    log_debug("Init begin");

    BspIwdgInit(IWDG_Prescaler_32, 4000 );   // 2.7s IWDG reset
    BspGpioInit();
    BspTimeInit();
    DevButtonInit();
    BspWs2812Init();
    BspAdcInit();
    BspGpioSetVout(1);

// log_debug("ADC raw vout=%u ibus=%u", 1, 1);
// log_info("VBUS=%u mV, VOUT=%.3f V", 1, 1.1);
// log_warn("VBUS %u mV 低于门限", 1);
// log_error("PD 协商超时, state=%d", 1);
// log_fatal("IWDG 复位前现场: ...");
    log_debug("Init OK");

    // {
    //     static const uint8_t au8Grb[12] = { 0u,255u,0u,  0u,255u,0u,  0u,255u,0u,  0u,255u,0u }; /* 4 颗全绿 */
    //     (void)BspWs2812LoadBytes(au8Grb, 12u);
    //     (void)BspWs2812Show();
    // }
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
    System_Init();
    User_Init();
    while (1)
    {
        PT_TASK_REG(0, BspIwdgTask);
        PT_TASK_REG(1, BspSystemTask);
        PT_TASK_REG(2, BspSensorTask);
        PT_TASK_REG(3, UserDisplayTask);
        PT_TASK_REG(4, UsrButtonTask);
        
    }
}
