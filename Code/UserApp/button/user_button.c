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
 */

#include "user_button.h"

/* 单键事件为等组合伙伴而扣留的时长（须覆盖两键单击事件的最大时差） */
#define USR_BTN_COMBO_WAIT_MS     (100u)
#define USR_BTN_COMBO_WAIT_TICKS  (USR_BTN_COMBO_WAIT_MS / DEV_BTN_SCAN_MS)

#define USR_BTN_SUB_NUM           (sizeof(s_atButtonSub) / sizeof(s_atButtonSub[0]))

/* 订阅表：组合条目优先匹配；注册多条组合时表序即优先级，更长的组合写在前 */
static const tUsrButtonSubDef s_atButtonSub[] =
{
    { E_BSP_KEY_1,               E_DEV_BTN_SINGLE_CLICK, UsrButtonValueDec },
    { E_BSP_KEY_2,               E_DEV_BTN_SINGLE_CLICK, UsrButtonValueInc },
    { E_BSP_KEY_1 | E_BSP_KEY_2, E_DEV_BTN_SINGLE_CLICK, UsrButtonValueDec },
};

/* 单键事件扣留槽：等待组合伙伴期间暂存，下标为键位索引 */
typedef struct tUsrButtonHoldDef
{
    eDevButtonEventDef eEvent;   /* 正在扣留的事件；E_DEV_BTN_NONE = 空槽 */
    uint8_t            u8Ticks;  /* 已扣留的任务拍数 */
} tUsrButtonHoldDef;

static tUsrButtonHoldDef s_atHold[E_BSP_KEY_NUM];

/**
 * @brief  判断事件是否被某个多键组合条目引用。
 * @param[in] eEvent 事件。
 * @return 1=被组合条目引用；0=否。
 */
static uint8_t UsrButtonEventHasCombo(eDevButtonEventDef eEvent)
{
    uint8_t u8Entry;

    for (u8Entry = 0u; u8Entry < (uint8_t)USR_BTN_SUB_NUM; u8Entry++)
    {
        if ((s_atButtonSub[u8Entry].eEvent == eEvent) &&
            ((s_atButtonSub[u8Entry].u8KeyMask & (s_atButtonSub[u8Entry].u8KeyMask - 1u)) != 0u))
        {
            return 1u;
        }
    }
    return 0u;
}

/**
 * @brief  查找（单键位值, 事件）对应的订阅条目。
 * @param[in] u8KeyBit 单键位值。
 * @param[in] eEvent 事件。
 * @return 条目指针；无则返回 NULL。
 */
static const tUsrButtonSubDef *UsrButtonFindSingle(uint8_t u8KeyBit, eDevButtonEventDef eEvent)
{
    uint8_t u8Entry;

    for (u8Entry = 0u; u8Entry < (uint8_t)USR_BTN_SUB_NUM; u8Entry++)
    {
        if ((s_atButtonSub[u8Entry].eEvent == eEvent) &&
            (s_atButtonSub[u8Entry].u8KeyMask == u8KeyBit))
        {
            return &s_atButtonSub[u8Entry];
        }
    }
    return NULL;
}

/**
 * @brief  撤销掩码内各键对事件的扣留（组合命中后调用）。
 * @param[in] u8KeyMask 键位值掩码。
 * @param[in] eEvent 事件。
 */
static void UsrButtonHoldCancel(uint8_t u8KeyMask, eDevButtonEventDef eEvent)
{
    uint8_t u8Index;

    for (u8Index = 0u; u8Index < (uint8_t)E_BSP_KEY_NUM; u8Index++)
    {
        if (((u8KeyMask & (uint8_t)(1u << u8Index)) != 0u) &&
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
    uint8_t                 u8KeyBit;
    eDevButtonEventDef      eEvent;
    const tUsrButtonSubDef *ptSub;

    for (u8Index = 0u; u8Index < (uint8_t)E_BSP_KEY_NUM; u8Index++)
    {
        if (s_atHold[u8Index].eEvent == E_DEV_BTN_NONE)
        {
            continue;
        }

        u8KeyBit = (uint8_t)(1u << u8Index);
        eEvent   = s_atHold[u8Index].eEvent;

        if ((DevButtonPendingMask(eEvent) & u8KeyBit) == 0u)
        {
            s_atHold[u8Index].eEvent = E_DEV_BTN_NONE;    /* 已被组合条目消费 */
            continue;
        }

        s_atHold[u8Index].u8Ticks++;
        if (s_atHold[u8Index].u8Ticks >= (uint8_t)USR_BTN_COMBO_WAIT_TICKS)
        {
            s_atHold[u8Index].eEvent = E_DEV_BTN_NONE;
            ptSub = UsrButtonFindSingle(u8KeyBit, eEvent);
            DevButtonClearEvent(u8KeyBit, eEvent);
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
    uint8_t                 u8Pending;
    uint8_t                 u8KeyBit;
    uint8_t                 u8EventIdx;
    const tUsrButtonSubDef *ptSub;

    /* 1) 组合键优先：组合条目内每个键都有同一事件待取时命中 */
    for (u8Entry = 0u; u8Entry < (uint8_t)USR_BTN_SUB_NUM; u8Entry++)
    {
        ptSub = &s_atButtonSub[u8Entry];
        if ((ptSub->u8KeyMask & (ptSub->u8KeyMask - 1u)) == 0u)
        {
            continue;                                     /* 单键条目 */
        }

        u8Pending = DevButtonPendingMask(ptSub->eEvent);
        if ((u8Pending & ptSub->u8KeyMask) == ptSub->u8KeyMask)
        {
            ptSub->fun();
            DevButtonClearEvent(ptSub->u8KeyMask, ptSub->eEvent);
            UsrButtonHoldCancel(ptSub->u8KeyMask, ptSub->eEvent);
        }
    }

    /* 2) 单键条目：被组合引用的事件先扣留，其余立即分发 */
    for (u8Entry = 0u; u8Entry < (uint8_t)USR_BTN_SUB_NUM; u8Entry++)
    {
        ptSub = &s_atButtonSub[u8Entry];
        if ((ptSub->u8KeyMask & (ptSub->u8KeyMask - 1u)) != 0u)
        {
            continue;                                     /* 组合条目 */
        }

        u8KeyBit  = ptSub->u8KeyMask;
        u8Pending = DevButtonPendingMask(ptSub->eEvent);
        if ((u8Pending & u8KeyBit) == 0u)
        {
            continue;
        }

        if (UsrButtonEventHasCombo(ptSub->eEvent) == 0u)
        {
            ptSub->fun();                                 /* 无组合需求：立即分发 */
            DevButtonClearEvent(u8KeyBit, ptSub->eEvent);
            continue;
        }

        u8Index = 0u;
        while ((u8Index < (uint8_t)E_BSP_KEY_NUM) && (u8KeyBit != (uint8_t)(1u << u8Index)))
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
            DevButtonClearEvent(u8KeyBit, ptSub->eEvent);
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

        for (u8Entry = 0u; u8Entry < (uint8_t)USR_BTN_SUB_NUM; u8Entry++)
        {
            if (s_atButtonSub[u8Entry].eEvent == (eDevButtonEventDef)u8EventIdx)
            {
                break;                                    /* 表内引用的事件不清 */
            }
        }
        if (u8Entry < (uint8_t)USR_BTN_SUB_NUM)
        {
            continue;
        }

        DevButtonClearEvent(0xFFu, (eDevButtonEventDef)u8EventIdx);
    }
}

uint16_t UsrButtonTask(void)
{
    PT_BEGIN()
    {
    }

    while (1)
    {
        PT_WAIT_UNTIL(DEV_BTN_SCAN_MS / OS_TICK_MS);   /* 5ms 时间片 */
        UsrButtonProcessEvents();
    }

    PT_END();
}
