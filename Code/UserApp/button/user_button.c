/**
 * @file    user_button.c
 * @brief   按键应用层：订阅表把（键组合, 事件）映射为产品行为，支持组合键注册。
 *
 * @note    扫描与消抖在 Device 层完成，本层只做"事件 → 业务"分发。
 *          键值 E_BSP_KEY_x 为位值（1/2/4）：单键写一个键值，组合键写多个键
 *          按位或（如 E_BSP_KEY_1 | E_BSP_KEY_2）。
 *
 *          组合键判定：同一拍内，组合条目中的每个键都产生了同一事件则命中，
 *          命中后吃掉这些键的该事件（单键条目不再触发）。由于两个键几乎同时
 *          抬起时，各自的单击事件最多相差 DEV_BTN_CLICK_WINDOW 才先后生成，
 *          被"组合条目引用的事件"会先扣留 USR_BTN_COMBO_WAIT_MS 等待伙伴，
 *          无人配对到期后按单键照常分发；未被组合引用的事件不受影响、立即分发。
 *
 *          订阅表改为运行期注册：先 UsrButtonInit() 清表，再用 UsrButtonRegister()
 *          逐条登记；注册顺序即匹配优先级，更长的组合条目要注册在前。
 */

#include "user_button.h"
#include "Task.h"                    /* PT_BEGIN/PT_WAIT_UNTIL/OS_TICK_MS */
#include <string.h>

/* 单键事件为等组合伙伴而扣留的时长（须覆盖两键单击事件的最大时差） */
#define USR_BTN_COMBO_WAIT_MS     (100u)
#define USR_BTN_COMBO_WAIT_TICKS  (USR_BTN_COMBO_WAIT_MS / DEV_BTN_SCAN_MS)

/* 订阅表：运行期由 UsrButtonRegister() 填写，s_u8SubNum 为已登记条目数 */
static tUsrButtonSubDef s_atButtonSub[USR_BTN_SUB_MAX];
static uint8_t          s_u8SubNum = 0u;

/* 单键事件扣留槽：等待组合伙伴期间暂存，下标为键位索引 */
typedef struct tUsrButtonHoldDef
{
    eDevButtonEventDef eEvent;   /* 正在扣留的事件；E_DEV_BTN_NONE = 空槽 */
    uint8_t            u8Ticks;  /* 已扣留的任务拍数 */
} tUsrButtonHoldDef;

static tUsrButtonHoldDef s_atHold[E_BSP_KEY_NUM];

/* ========================================================================== *
 *  订阅表注册
 * ========================================================================== */

void UsrButtonInit(void)
{
    s_u8SubNum = 0u;
    memset(s_atButtonSub, 0, sizeof(s_atButtonSub));
    memset(s_atHold, 0, sizeof(s_atHold));
}

/**
 * @brief  注册一条订阅：某键（或键组合）产生某事件时调用 fun。
 * @param[in] u16KeyMask 键位值掩码：单键写一个 E_BSP_KEY_x，组合键按位或。
 * @param[in] eEvent 触发事件，须为 E_DEV_BTN_NONE 与 E_DEV_BTN_COUNT 之间的业务事件。
 * @param[in] fun 命中后的回调。
 * @return 1=注册成功；0=掩码非法 / 事件非法 / 回调为空 / 同键同事件重复 / 表已满。
 * @note   注册顺序即匹配优先级，更长的组合条目要注册在前，否则会被短条目抢先。
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

/**
 * @brief  判断事件是否被某个多键组合条目引用。
 * @param[in] eEvent 事件。
 * @return 1=被组合条目引用；0=否。
 */
static uint8_t UsrButtonEventHasCombo(eDevButtonEventDef eEvent)
{
    uint8_t u8Entry;

    for (u8Entry = 0u; u8Entry < s_u8SubNum; u8Entry++)
    {
        if ((s_atButtonSub[u8Entry].eEvent == eEvent) &&
            ((s_atButtonSub[u8Entry].u16KeyMask & (s_atButtonSub[u8Entry].u16KeyMask - 1u)) != 0u))
        {
            return 1u;
        }
    }
    return 0u;
}

/**
 * @brief  查找（单键位值, 事件）对应的订阅条目。
 * @param[in] u16KeyBit 单键位值。
 * @param[in] eEvent 事件。
 * @return 条目指针；无则返回 NULL。
 */
static const tUsrButtonSubDef *UsrButtonFindSingle(uint16_t u16KeyBit, eDevButtonEventDef eEvent)
{
    uint8_t u8Entry;

    for (u8Entry = 0u; u8Entry < s_u8SubNum; u8Entry++)
    {
        if ((s_atButtonSub[u8Entry].eEvent == eEvent) &&
            (s_atButtonSub[u8Entry].u16KeyMask == u16KeyBit))
        {
            return &s_atButtonSub[u8Entry];
        }
    }
    return NULL;
}

/**
 * @brief  撤销掩码内各键对事件的扣留（组合命中后调用）。
 * @param[in] u16KeyMask 键位值掩码。
 * @param[in] eEvent 事件。
 */
static void UsrButtonHoldCancel(uint16_t u16KeyMask, eDevButtonEventDef eEvent)
{
    uint8_t u8Index;

    for (u8Index = 0u; u8Index < (uint8_t)E_BSP_KEY_NUM; u8Index++)
    {
        if (((u16KeyMask & (uint16_t)(1u << u8Index)) != 0u) &&
            (s_atHold[u8Index].eEvent == eEvent))
        {
            s_atHold[u8Index].eEvent = E_DEV_BTN_NONE;
        }
    }
}

/**
 * @brief  扣留槽到期处理：等满配合窗仍无人配对则按单键分发。
 */
static void UsrButtonHoldTick(void)
{
    uint8_t                 u8Index;
    uint16_t                u16KeyBit;
    eDevButtonEventDef      eEvent;
    const tUsrButtonSubDef *ptSub;

    for (u8Index = 0u; u8Index < (uint8_t)E_BSP_KEY_NUM; u8Index++)
    {
        if (s_atHold[u8Index].eEvent == E_DEV_BTN_NONE)
        {
            continue;
        }

        u16KeyBit = (uint16_t)(1u << u8Index);
        eEvent   = s_atHold[u8Index].eEvent;

        if ((DevButtonPendingMask(eEvent) & u16KeyBit) == 0u)
        {
            s_atHold[u8Index].eEvent = E_DEV_BTN_NONE;    /* 已被组合条目消费 */
            continue;
        }

        s_atHold[u8Index].u8Ticks++;
        if (s_atHold[u8Index].u8Ticks >= (uint8_t)USR_BTN_COMBO_WAIT_TICKS)
        {
            s_atHold[u8Index].eEvent = E_DEV_BTN_NONE;
            ptSub = UsrButtonFindSingle(u16KeyBit, eEvent);
            DevButtonClearEvent(u16KeyBit, eEvent);
            if (ptSub != NULL)
            {
                ptSub->fun();
            }
        }
    }
}

void UsrButtonProcessEvents(void)
{
    uint8_t                 u8Entry;
    uint8_t                 u8Index;
    uint16_t                u16Pending;
    uint16_t                u16KeyBit;
    uint8_t                 u8EventIdx;
    const tUsrButtonSubDef *ptSub;

    /* 1) 组合键优先：组合条目内每个键都有同一事件待取时命中 */
    for (u8Entry = 0u; u8Entry < s_u8SubNum; u8Entry++)
    {
        ptSub = &s_atButtonSub[u8Entry];
        if ((ptSub->u16KeyMask & (ptSub->u16KeyMask - 1u)) == 0u)
        {
            continue;                                     /* 单键条目 */
        }

        u16Pending = DevButtonPendingMask(ptSub->eEvent);
        if ((u16Pending & ptSub->u16KeyMask) == ptSub->u16KeyMask)
        {
            ptSub->fun();
            DevButtonClearEvent(ptSub->u16KeyMask, ptSub->eEvent);
            UsrButtonHoldCancel(ptSub->u16KeyMask, ptSub->eEvent);
        }
    }

    /* 2) 单键条目：被组合引用的事件先扣留，其余立即分发 */
    for (u8Entry = 0u; u8Entry < s_u8SubNum; u8Entry++)
    {
        ptSub = &s_atButtonSub[u8Entry];
        if ((ptSub->u16KeyMask & (ptSub->u16KeyMask - 1u)) != 0u)
        {
            continue;                                     /* 组合条目 */
        }

        u16KeyBit  = ptSub->u16KeyMask;
        u16Pending = DevButtonPendingMask(ptSub->eEvent);
        if ((u16Pending & u16KeyBit) == 0u)
        {
            continue;
        }

        if (UsrButtonEventHasCombo(ptSub->eEvent) == 0u)
        {
            ptSub->fun();                                 /* 无组合需求：立即分发 */
            DevButtonClearEvent(u16KeyBit, ptSub->eEvent);
            continue;
        }

        u8Index = 0u;
        while ((u8Index < (uint8_t)E_BSP_KEY_NUM) && (u16KeyBit != (uint16_t)(1u << u8Index)))
        {
            u8Index++;
        }
        if (u8Index >= (uint8_t)E_BSP_KEY_NUM)
        {
            continue;
        }

        if (s_atHold[u8Index].eEvent == E_DEV_BTN_NONE)
        {
            s_atHold[u8Index].eEvent  = ptSub->eEvent;
            s_atHold[u8Index].u8Ticks = 0u;
        }
        else if (s_atHold[u8Index].eEvent != ptSub->eEvent)
        {
            /* 槽被同键的另一事件占用：放弃扣留立即分发，避免事件滞留 */
            ptSub->fun();
            DevButtonClearEvent(u16KeyBit, ptSub->eEvent);
        }
    }

    /* 3) 扣留到期：无人配对则按单键分发 */
    UsrButtonHoldTick();

    /* 4) 订阅表未引用的事件直接丢弃，防止待取位长期滞留 */
    for (u8EventIdx = (uint8_t)E_DEV_BTN_DOWN; u8EventIdx < (uint8_t)E_DEV_BTN_COUNT; u8EventIdx++)
    {
        if (UsrButtonEventHasCombo((eDevButtonEventDef)u8EventIdx) != 0u)
        {
            continue;
        }

        for (u8Entry = 0u; u8Entry < s_u8SubNum; u8Entry++)
        {
            if (s_atButtonSub[u8Entry].eEvent == (eDevButtonEventDef)u8EventIdx)
            {
                break;                                    /* 表内引用的事件不清 */
            }
        }
        if (u8Entry < s_u8SubNum)
        {
            continue;
        }

        DevButtonClearEvent(0xFFFFu, (eDevButtonEventDef)u8EventIdx);
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
