/**
 * @file    bsp_time.h
 * @brief   系统时间片节拍（1ms tick）底层驱动头文件
 *******************************************************************************
 * @note    使用 TIM3 更新中断产生 1ms 节拍，为 Task.h 的 protothread 调度器
 *          提供时基，在中断内递减 PT_TICK[] 数组，并驱动按键扫描。
 *
 *          选型依据（见 CH32X035_1ms_tick_analysis.md）：
 *            - CH32X035 仅有 TIM1/TIM2（高级）+ TIM3（通用），无 TIM4；
 *            - TIM3 用于 1ms 系统节拍，TIM1 用于 WS2812 PWM，TIM2 仍空闲；
 *            - 纯内部 CK_INT 时钟源，不需要任何 GPIO/AFIO 重映射。
 *
 *          参数：fTIMxCLK = HCLK = 48MHz
 *                PSC = 47  -> CK_CNT = 1MHz
 *                ARR = 999 -> 更新周期 = 1000 / 1MHz = 1ms
 *******************************************************************************
 */

#ifndef __BSP_TIME_H__
#define __BSP_TIME_H__

#ifdef __cplusplus
extern "C" {
#endif
#include "user_global.h"

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

#ifdef __cplusplus
}
#endif

#endif /* __BSP_TICK_H__ */
