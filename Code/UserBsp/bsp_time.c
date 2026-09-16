/**
 * @file    bsp_tick.c
 * @brief   系统时间片节拍（1ms tick）底层驱动实现
 *******************************************************************************
 * @note    实现要点：
 *          1) TIM3 的 CK_INT 来自 HB 总线时钟（= HCLK = 48MHz），纯内部时钟源；
 *          2) 48MHz / (47+1) = 1MHz 计数；1MHz / (999+1) = 1kHz -> 1ms；
 *          3) TIM_TimeBaseInit() 末尾会置 UG 位，返回时 UIF 已为 1，
 *             因此必须在使能中断前先 TIM_ClearFlag()，否则会立刻误进一次中断；
 *          4) UIF 是 RW0（硬件置位、软件清零），中断内必须清。
 *******************************************************************************
 */

#include "bsp_time.h"
#include "Task.h"

/* ========================================================================== *
 *  全局变量
 * ========================================================================== */

/**
 * @brief protothread 任务定时器数组
 * @note  Task.h 中仅有 extern 声明，全工程必须且只能在此处定义一次。
 *        调度器通过 PT_TASK_REG() 写入"下次唤醒倒计时"，TIM3 中断里递减。
 */
volatile uint32_t PT_TICK[TASK_MAX] = {0u};

/** @brief 系统毫秒计数 */
static volatile uint32_t s_u32TickMs = 0u;

/* ========================================================================== *
 *  函数实现
 * ========================================================================== */

void BspTickInit(void)
{
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    NVIC_InitTypeDef        NVIC_InitStructure;

    memset(&TIM_TimeBaseStructure, 0, sizeof(TIM_TimeBaseStructure));
    memset(&NVIC_InitStructure, 0, sizeof(NVIC_InitStructure));

    /* 1) 使能 TIM3 时钟（APB1PCENR bit1） */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);

    /* 2) 1ms 时基：PSC=47 -> 1MHz；ARR=999 -> 1ms */
    TIM_TimeBaseStructure.TIM_Prescaler         = (uint16_t)(SystemCoreClock / 1000000u) - 1u; /* 48-1 = 47 */
    TIM_TimeBaseStructure.TIM_Period            = (uint16_t)(1000u / BOARD_TICK_MS) - 1u;      /* 1000-1 = 999 */
    TIM_TimeBaseStructure.TIM_ClockDivision     = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode       = TIM_CounterMode_Up;   /* TIM3 恒为增计数，此成员被忽略 */
    TIM_TimeBaseStructure.TIM_RepetitionCounter = 0;                    /* TIM3 无 RPTCR，此成员被忽略 */
    TIM_TimeBaseInit(TIM3, &TIM_TimeBaseStructure);

    /* 3) 清掉 UG 位产生的更新标志，避免使能中断后立即补一次中断 */
    TIM_ClearFlag(TIM3, TIM_FLAG_Update);

    /* 4) 使能更新中断（TIM3_DMAINTENR.UIE） */
    TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE);

    /* 5) NVIC（PFIC）配置：TIM3_IRQn = 54 */
    NVIC_InitStructure.NVIC_IRQChannel                   = TIM3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority  = 0;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    /* 6) 启动计数 */
    TIM_Cmd(TIM3, ENABLE);
}

uint32_t BspTickGetMs(void)
{
    return s_u32TickMs;
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

        s_u32TickMs++;
        TASK_TICK_UPDATE();
        BspButtonScanTick();
    }
}
