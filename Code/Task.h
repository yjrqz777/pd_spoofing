/**
 * @file    Task.h
 * @brief   Protothread 协程任务调度器
 *******************************************************************************
 * @note    基于 Protothread（轻量级协程）实现的任务调度框架。
 *          每个任务是一个使用 PT_BEGIN/PT_END 包裹的 C 函数，
 *          通过 PT_WAIT_UNTIL 实现非阻塞延时等待。
 *
 *          调度原理：定时器（1ms 中断）递减 PT_TICK 计数，
 *          当计数归零时触发任务继续执行，实现周期调度。
 *******************************************************************************
 */

#ifndef __TASK_H__
#define __TASK_H__


/**
 * @name   Protothread 状态常量
 * @{
 */
#define PT_WAITING  (0u)  /**< 任务等待中（定时器未到） */
#define PT_YIELDED  (1u)  /**< 任务主动让出 CPU */
#define PT_EXITED   (2u)  /**< 任务已退出 */
#define PT_ENDED    (3u)  /**< 任务正常结束 */
/** @} */

/** @brief 最大任务数 */
#define TASK_MAX (20u)

/** @brief 定时器间隔（单位：毫秒） */
#define OS_TICK_MS (1u)

/** @brief 全局任务定时器数组（外部声明） */
extern volatile uint32_t PT_TICK[TASK_MAX];

/**
 * @brief  注册并执行 protothread 任务
 * @param[in] Rank  任务优先级/索引（0 ~ TASK_MAX-1）
 * @param[in] Func  任务函数指针（返回 unsigned int 的 PT 函数）
 * @note   首次调用时执行任务函数，将返回值（延时时间）存入 PT_TICK。
 *         之后 PT_TICK 递减至 0 时重新触发任务。
 *         重复注册同一优先级无效。
 */
#define PT_TASK_REG(Rank, Func)   \
    do                            \
    {                             \
        if ((uint32_t)(Rank) >= TASK_MAX) \
        {                         \
            break;                \
        }                         \
        if (PT_TICK[Rank] == 0)     \
        {                         \
            PT_TICK[Rank] = Func(); \
        }                         \
    } while (0)

/**
 * @brief  更新所有任务的定时器计数（在 1ms 定时器中断中调用）
 * @note   遍历 PT_TICK 数组，将所有大于 0 的计数减 1。
 *         当计数归零时，下次主循环调度将触发对应任务继续执行。
 */
#define TASK_TICK_UPDATE()                                \
    do                                               \
    {                                                \
        uint8_t i;                                      \
        for (i = 0u; i < TASK_MAX; i++)                 \
        {                                            \
            if (PT_TICK[i] > 0)                  \
            {                                        \
                PT_TICK[i]--;                    \
            }                                        \
        }                                            \
    } while (0)

/**
 * @brief  启动 protothread 任务
 * @note   必须作为任务函数的第一条语句。
 *         展开为 switch-case 状态机框架，通过 pt_lc 记录断点位置。
 */
#define PT_BEGIN()            \
    {                           \
        static uint32_t pt_lc = 0u; static uint32_t u32WaitTimeSave = 0u; \
        switch (pt_lc)       \
        {                       \
        default:

/**
 * @brief  结束 protothread 任务
 * @note   必须作为任务函数的最后一条语句。
 *         返回 PT_ENDED 状态，重置断点位置。
 */
#define PT_END()     \
    }                  \
    ;                  \
    pt_lc = 0u;      \
    return PT_ENDED;   \
    }

/**
 * @brief  等待指定时间后继续执行
 * @param[in] u32WaitTime  等待时间（单位为 OS_TICK_MS 的倍数）
 * @note   保存当前行号为断点位置，返回等待时间。
 *         下次触发时通过 switch-case 跳转到此行继续执行。
 */
#define PT_WAIT_UNTIL(u32WaitTime) \
    do                              \
    {                               \
        u32WaitTimeSave = (uint32_t)(u32WaitTime); \
        pt_lc = __LINE__;        \
        return (uint16_t)u32WaitTimeSave; \
    } while (0);                    \
    case __LINE__:;

/**
 * @brief  重启 protothread 任务（从头开始执行）
 * @note   重置断点位置为 0，返回 PT_WAITING 状态，
 *         下次调度时从 PT_BEGIN 开始执行。
 */
#define PT_RESTART()     \
    do                     \
    {                      \
        pt_lc = 0u;      \
        return PT_WAITING; \
    } while (0)

/**
 * @brief  退出 protothread 任务（保留最后等待时间）
 * @note   返回上次保存的等待时间，下次调度时重新等待相同时间，
 *         然后从断点后继续执行。
 */
#define PT_EXIT()       \
    do                    \
    {                     \
        return (uint16_t)u32WaitTimeSave; \
    } while (0)

#endif /* __TASK_H__ */
