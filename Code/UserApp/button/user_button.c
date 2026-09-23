/**
 * @file    user_button.c
 * @brief   按键应用层：订阅表把（键值掩码, 事件）映射为产品行为。
 *
 * @note    扫描、消抖、组合键判定都在 Device 层完成，本层只做"事件 → 业务"分发：
 *          时间片内从队列逐条取走事件，按（键值掩码, 事件）精确查表，命中就调回调。
 *          键值 E_BSP_KEY_x 为位值（1/2/4）：单键写一个键值，组合键写多个键
 *          按位或（如 E_BSP_KEY_1 | E_BSP_KEY_2）。
 *
 *          要收到某个键（或组合键）的事件，Device 层必须用 DevButtonRegisterKey()
 *          注册同一个键值掩码，否则该事件根本不会产生。
 *
 *          订阅表为运行期注册：先 UsrButtonInit() 清表，再用 UsrButtonRegister()
 *          逐条登记；键值掩码与事件都相同的条目只保留第一条。
 */

#include "user_button.h"
#include "Task.h"                    /* PT_BEGIN/PT_WAIT_UNTIL/OS_TICK_MS */
#include <string.h>

/* 订阅表：运行期由 UsrButtonRegister() 填写，s_u8SubNum 为已登记条目数 */
static tUsrButtonSubDef s_atButtonSub[USR_BTN_SUB_MAX];
static uint8_t          s_u8SubNum = 0u;

/* ========================================================================== *
 *  订阅表注册
 * ========================================================================== */

void UsrButtonInit(void)
{
    s_u8SubNum = 0u;
    memset(s_atButtonSub, 0, sizeof(s_atButtonSub));
}

/**
 * @brief  注册一条订阅：某键（或键组合）产生某事件时调用 fun。
 * @param[in] u16KeyMask 键值掩码：单键写一个 E_BSP_KEY_x，组合键按位或。
 * @param[in] eEvent 触发事件，须为 E_DEV_BTN_NONE 与 E_DEV_BTN_COUNT 之间的业务事件。
 * @param[in] fun 命中后的回调。
 * @return 1=注册成功；0=掩码非法 / 事件非法 / 回调为空 / 同键同事件重复 / 表已满。
 * @note   键值掩码须与 Device 层 DevButtonRegisterKey() 注册的一致。
 */
uint8_t UsrButtonRegister(uint16_t u16KeyMask, eDevButtonEventDef eEvent, UsrButtonFunDef fun)
{
    uint8_t u8Entry;

    if ((u16KeyMask == 0u) ||
        ((u16KeyMask & (uint16_t)~((1u << (uint8_t)E_BSP_KEY_NUM) - 1u)) != 0u))
    {
        return 0u;
    }
    if ((eEvent <= E_DEV_BTN_NONE) || (eEvent >= E_DEV_BTN_COUNT) || (fun == NULL))
    {
        return 0u;
    }

    for (u8Entry = 0u; u8Entry < s_u8SubNum; u8Entry++)
    {
        if ((s_atButtonSub[u8Entry].u16KeyMask == u16KeyMask) &&
            (s_atButtonSub[u8Entry].eEvent == eEvent))
        {
            return 0u;
        }
    }

    if (s_u8SubNum >= (uint8_t)USR_BTN_SUB_MAX)
    {
        return 0u;
    }

    s_atButtonSub[s_u8SubNum].u16KeyMask = u16KeyMask;
    s_atButtonSub[s_u8SubNum].eEvent    = eEvent;
    s_atButtonSub[s_u8SubNum].fun       = fun;
    s_u8SubNum++;

    return 1u;
}

/**
 * @brief  注册原静态表的默认条目，不调用则订阅表为空。
 */
void UsrButtonRegisterDefault(void)
{
    (void)UsrButtonRegister(E_BSP_KEY_1, E_DEV_BTN_SINGLE_CLICK, UsrButtonValueDec);
    (void)UsrButtonRegister(E_BSP_KEY_2, E_DEV_BTN_SINGLE_CLICK, UsrButtonValueInc);
    (void)UsrButtonRegister(E_BSP_KEY_1 | E_BSP_KEY_2, E_DEV_BTN_SINGLE_CLICK, UsrButtonValueDec);
}

/* ========================================================================== *
 *  事件分发
 * ========================================================================== */

/**
 * @brief  取走并分发队列里所有待处理事件。
 * @note   主循环上下文调用（UsrButtonTask 的 5ms 时间片）。事件逐条消费，
 *         订阅表里没有对应条目的事件直接丢弃。
 */
void UsrButtonProcessEvents(void)
{
    tDevButtonEventDef tEvent;
    uint8_t            u8Entry;

    while (DevButtonPopEvent(&tEvent) != 0u)
    {
        for (u8Entry = 0u; u8Entry < s_u8SubNum; u8Entry++)
        {
            if ((s_atButtonSub[u8Entry].u16KeyMask == tEvent.u16KeyMask) &&
                (s_atButtonSub[u8Entry].eEvent == tEvent.eEvent))
            {
                s_atButtonSub[u8Entry].fun();
                break;
            }
        }
    }
}

uint16_t UsrButtonTask(void)
{
    PT_BEGIN()
    {
        UsrButtonInit();
        UsrButtonRegisterDefault();
    }

    while (1)
    {
        PT_WAIT_UNTIL(DEV_BTN_SCAN_MS / OS_TICK_MS);   /* 5ms 时间片 */
        UsrButtonProcessEvents();
    }

    PT_END();
}
