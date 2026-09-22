/**
 * @file    bsp_time.h
 * @brief   定时器底层驱动头文件（TIM3 系统节拍 + TIM1 时基）
 *******************************************************************************
 * @note    本模块是本工程唯一的“定时器资源”模块，两个定时器分工如下：
 *
 *          1) TIM3 —— 1ms 系统节拍
 *             为 Task.h 的 protothread 调度器提供时基，在中断内递减 PT_TICK[]。
 *             fTIMxCLK = HCLK = 48MHz；PSC = 47 -> CK_CNT = 1MHz；ARR = 999 -> 1ms
 *
 *          2) TIM1 —— WS2812 位时序时基（CH1 输出，CH2~CH4 空闲）
 *             时基由 WS2812 的位时序决定，CH1~CH4 共用同一计数器，因此四个通道
 *             频率必然相同（800kHz），各通道只能独立设置占空比。
 *             需要另一路独立频率的 PWM 请用 TIM2；输入捕获/触发源可共用本时基。
 *             CK_CNT = 48MHz / (PSC+1) = 8MHz（PSC = 5）；周期 = 1.25us（ARR = 9）
 *
 *          选型依据见 CH32X035_1ms_tick_analysis.md：CH32X035 仅有
 *          TIM1/TIM2（高级）+ TIM3（通用），无 TIM4；TIM2 仍空闲。
 *******************************************************************************
 */

#ifndef __BSP_TIME_H__
#define __BSP_TIME_H__

#ifdef __cplusplus
extern "C" {
#endif
#include "user_global.h"

/* ========================================================================== *
 *  TIM1 时基参数（WS2812 位时序，CH1~CH4 共用）
 * ========================================================================== */
#define BSP_TIM1_CK_CNT_HZ   (8000000u)
#define BSP_TIM1_PRESCALER   ((uint16_t)((SystemCoreClock / BSP_TIM1_CK_CNT_HZ) - 1u)) /* 5 */
#define BSP_TIM1_PERIOD      (10u - 1u)                                               /* 9 */

/**
 * @brief  初始化并启动 1ms 系统节拍（TIM3）
 * @note   调用顺序要求：先 SystemCoreClockUpdate()，再调用本函数。
 *         函数返回前已完成：时钟使能、时基配置、UIF 清零、
 *         更新中断使能、NVIC 配置、计数器启动。
 */
void BspTimeInit(void);

/**
 * @brief  获取系统上电以来的毫秒计数
 * @return 毫秒计数（TIM3 中断内累加）
 */
uint32_t BspTimeGetMs(void);

/**
 * @brief  配置并启动 TIM1 时基（含 TIM1 时钟使能、ARR 预装载）
 * @note   只负责“时基”，通道配置（CCMR/CCER/CCR、引脚、DMA）由使用方负责，
 *         例如 bsp_ws2812.c 只配置 CH1 与 DMA1_Channel5。
 *         重复调用只保留首次配置，避免后加入的通道改掉 WS2812 的位周期。
 *         调用顺序要求：先 SystemCoreClockUpdate()，且在任何 TIM1 通道配置之前。
 */
void BspTim1BaseInit(void);

/**
 * @brief  注册 1ms 节拍回调。
 * @param[in] pfHandler 回调函数；在 TIM3 更新中断上下文执行，必须极短、非阻塞；传 NULL 取消注册。
 * @note   回调以函数指针方式调用，本模块不依赖任何上层模块。
 */
void BspTimeAttachTickHandler(FuncPtr pfHandler);

/**
 * @brief  关闭全局中断（进入临界区）。
 * @note   只用于保护极短的“读-改-写”序列；不支持嵌套，进出必须成对。
 */
void BspIrqDisableAll(void);

/**
 * @brief  打开全局中断（退出临界区）。
 */
void BspIrqEnableAll(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_TIME_H__ */
