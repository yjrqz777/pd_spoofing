/**
 * @file    bsp_time.c
 * @brief   定时器底层驱动实现（TIM3 系统节拍 + TIM1 时基）
 *******************************************************************************
 * @note    实现要点：
 *          1) TIM3 的 CK_INT 来自 HB 总线时钟（= HCLK = 48MHz），纯内部时钟源；
 *          2) 48MHz / (47+1) = 1MHz 计数；1MHz / (999+1) = 1kHz -> 1ms；
 *          3) TIM_TimeBaseInit() 末尾会置 UG 位，返回时 UIF 已为 1，
 *             因此必须在使能中断前先 TIM_ClearFlag()，否则会立刻误进一次中断；
 *          4) UIF 是 RW0（硬件置位、软件清零），中断内必须清。
 *
 *          资源约定：TIM1 的时基只在本文件的 BspTim1BaseInit() 中配置，
 *          使用 TIM1 通道的模块（如 bsp_ws2812.c 用 CH1）不得再调用
 *          TIM_TimeBaseInit()，否则会把先配置好的位周期改掉。
 *******************************************************************************
 */

#include "bsp_time.h"
#include "Task.h"

volatile uint32_t PT_TICK[TASK_MAX] = {0u}; /* pd 任务定时器数组 */

static volatile uint32_t s_u32TimeMs = 0u;  /* 系统毫秒计数 */

/** @brief 1ms 节拍回调（在 TIM3 中断上下文执行，可为 NULL） */
static FuncPtr s_pfTickHandler = NULL;
/* ========================================================================== *
 *  函数实现
 * ========================================================================== */
void BspTime3Init(void)
{
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    NVIC_InitTypeDef        NVIC_InitStructure;

    memset(&TIM_TimeBaseStructure, 0, sizeof(TIM_TimeBaseStructure));
    memset(&NVIC_InitStructure, 0, sizeof(NVIC_InitStructure));

    /* 1) 使能 TIM3 时钟（APB1PCENR bit1） */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);

    /* 2) 1ms 时基：PSC=47 -> 1MHz；ARR=999 -> 1ms */
    TIM_TimeBaseStructure.TIM_Prescaler         = (uint16_t)(SystemCoreClock / 1000000u) - 1u; /* 48-1 = 47 */
    TIM_TimeBaseStructure.TIM_Period            = (uint16_t)(1000u / PD_TICK_MS) - 1u;      /* 1000-1 = 999 */
    TIM_TimeBaseStructure.TIM_ClockDivision     = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode       = TIM_CounterMode_Up;   /* TIM3 恒为增计数，此成员被忽略 */
    TIM_TimeBaseStructure.TIM_RepetitionCounter = 0;                    /* TIM3 无 RPTCR，此成员被忽略 */
    TIM_TimeBaseInit(TIM3, &TIM_TimeBaseStructure);

    /* 3) 清掉 UG 位产生的更新标志，避免使能中断后立即补一次中断 */
    TIM_ClearFlag(TIM3, TIM_FLAG_Update);

    /* 4) NVIC（PFIC）配置：TIM3_IRQn */
    NVIC_InitStructure.NVIC_IRQChannel                   = TIM3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority  = 0;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    /* 5) 使能更新中断（TIM3_DMAINTENR.UIE） */
    TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE);

    /* 6) 启动计数 */
    TIM_Cmd(TIM3, ENABLE);
}

void BspTim1BaseInit(void)
{
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
    memset(&TIM_TimeBaseInitStructure, 0, sizeof(TIM_TimeBaseInitStructure));

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1, ENABLE);   /* TIM1 时钟在 APB2，必须显式使能 */

    TIM_TimeBaseInitStructure.TIM_Prescaler         = BSP_TIM1_PRESCALER;  /* 5  -> CK_CNT = 8MHz */
    TIM_TimeBaseInitStructure.TIM_Period            = BSP_TIM1_PERIOD;     /* 9  -> 周期 1.25us  */
    TIM_TimeBaseInitStructure.TIM_ClockDivision     = TIM_CKD_DIV1;
    TIM_TimeBaseInitStructure.TIM_CounterMode       = TIM_CounterMode_Up;
    TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0u;
    TIM_TimeBaseInit(TIM1, &TIM_TimeBaseInitStructure);

    TIM_ARRPreloadConfig(TIM1, ENABLE);                    /* ARR 预装载：在更新事件后生效 */
    TIM_Cmd(TIM1, ENABLE);                                 /* 计数器总开关，CH1~CH4 共用 */

}

void BspTimeInit(void)
{
    BspTime3Init();                                        /* 系统节拍初始化（TIM3） */
}

uint32_t BspTimeGetMs(void)
{
    return s_u32TimeMs;
}

void BspTimeAttachTickHandler(FuncPtr pfHandler)
{
    s_pfTickHandler = pfHandler;      /* 只在初始化阶段注册，主循环启动后不再改动 */
}

void BspIrqDisableAll(void)
{
    __disable_irq();                  /* Core/core_riscv.h：csrc mstatus, MIE */
}

void BspIrqEnableAll(void)
{
    __enable_irq();                   /* Core/core_riscv.h：csrs mstatus, MIE */
}

/**
 * @brief  TIM3 更新中断服务函数（1ms 系统节拍）
 * @note   RISC-V 中断必须带 WCH-Interrupt-fast 属性（与 ch32x035_it.c 写法一致）。
 *         ISR 同时驱动按键 GPIO 采样和消抖，按键回调仍在主循环执行。
 */
void TIM3_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void TIM3_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM3, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM3, TIM_IT_Update);

        s_u32TimeMs++;
        TASK_TICK_UPDATE();

        if (s_pfTickHandler != NULL)
        {
            s_pfTickHandler();        /* 只调用注册进来的短函数，本模块不知道它是谁 */
        }
    }
}
