/**
 * @file    dev_button.c
 * @brief   按键设备层实现：状态机跑在 1ms 节拍中断内，事件经 SPSC 队列交给主循环。
 *
 * @note    中断路径（DevButtonTickHandler → DevButtonHandler）只做电平采样、消抖、
 *          状态机与入队。SPSC 队列是唯一的中断到主循环事件通道：单生产者为
 *          TIM3 中断，单消费者为主循环（DevButtonProcessEvents 调用点）。
 *          扫描对象与订阅关系都由 DevButtonRegister() 注册进来，本模块不写死任何键。
 *          中断路径只入队，调回调发生在主循环的 DevButtonProcessEvents() 里。
 *          禁止在中断路径中调用 log/printf、浮点、延时或阻塞等待。
 */

#include "dev_button.h"
#include "UserBsp/bsp_time.h"
#include <string.h>

#define DEV_BTN_ACTIVE_LEVEL   (0u)      /* 本板按键低电平有效 */

/* 扫描注册表：注册进来的键值掩码（单键或组合），表内顺序即上下文槽位顺序 */
static uint16_t s_aKeyMask[DEV_BTN_KEY_MAX];
static uint8_t  s_keyNum = 0u;

/* 订阅表：注册进来的（键值掩码, 事件）→ 回调，表内顺序即匹配顺序 */
static tDevButtonEventDef s_atSub[DEV_BTN_SUB_MAX];
static uint8_t            s_subNum = 0u;

/* SPSC 队列：中断侧只写 Tail，主循环侧只写 Head */
typedef struct
{
    tDevButtonEventDef atItem[DEV_BTN_QUEUE_SIZE];   /* 环形缓冲：键值掩码 + 事件 */
    uint16_t           u16Head;                      /* 头 消费者游标：主循环独占 */
    uint16_t           u16Tail;                      /* 尾 生产者游标：中断独占 */
} tDevButtonQueueDef;

static volatile tDevButtonQueueDef s_tQueue;

/* 按键状态机状态（本文件私有） */
typedef enum
{
    E_DEV_BTN_STATE_IDLE = 0,    /* 空闲 */
    E_DEV_BTN_STATE_PRESS,       /* 已按下 */
    E_DEV_BTN_STATE_RELEASE,     /* 已抬起，等待多击超时 */
    E_DEV_BTN_STATE_REPEAT,      /* 连击中的再次按下 */
    E_DEV_BTN_STATE_LONG_HOLD    /* 长按保持 */
} eDevButtonStateDef;

/* 单个扫描条目的上下文（本文件私有，长度由 DEV_BTN_KEY_MAX 在编译期确定） */
typedef struct
{
    uint16_t          u16Ticks;     /* 扫描计数 */
    uint8_t           u8Suppress;   /* 本轮点按被组合吃掉：不发单击/双击结果 */
    uint8_t           u8State;      /* 状态机状态 */
    uint8_t           u8Repeat;     /* 连击次数 */
    uint8_t           u8Debounce;   /* 消抖计数 */
    uint8_t           u8Level;      /* 当前（已消抖）电平：1=未按下, 0=按下 */
    uint8_t           u8HoldDiv;    /* 长按持续上报的节流计数 */
} tDevButtonDef;

static tDevButtonDef s_atButton[DEV_BTN_KEY_MAX];


/* ========================================================================== *
 *  以下为中断上下文代码
 * ========================================================================== */

/**
 * @brief  中断侧：把（键值掩码, 事件 id）压入 SPSC 队列。
 * @param[in] u16KeyMask 注册项的键值掩码（单键或组合）。
 * @param[in] eEvent 事件。
 * @note   只有 TIM3 中断调用，是队列的唯一生产者。队列满时丢弃本次事件，
 *         不覆盖尚未被主循环取走的旧事件。
 */
static void DevButtonQueueEvent(uint16_t u16KeyMask, eDevButtonEventDef eEvent)
{
    uint16_t u16Next;

    if ((eEvent <= E_DEV_BTN_NONE) || (eEvent >= E_DEV_BTN_COUNT))
    {
        return;
    }

    u16Next = (uint16_t)((s_tQueue.u16Tail + 1u) % (uint16_t)DEV_BTN_QUEUE_SIZE);
    if (u16Next == s_tQueue.u16Head)
    {
        return;                                     /* 队列满：丢弃本次事件 */
    }

    s_tQueue.atItem[s_tQueue.u16Tail].u16KeyMask = u16KeyMask;
    s_tQueue.atItem[s_tQueue.u16Tail].eEvent     = eEvent;
    s_tQueue.u16Tail = u16Next;
}

/**
 * @brief  读一个注册项的电平：掩码内所有键同时为有效电平才算按下。
 * @param[in] u16KeyMask 注册项的键值掩码。
 * @return 1=未按下, 0=按下。
 * @note   掩码的每个 bit 就是一个键值（E_BSP_KEY_x = 1/2/4），逐位取引脚电平；
 *         组合项按"全部按下"合成一个逻辑键，少一个键就整项未按下。
 */
static uint8_t DevButtonReadLevel(uint16_t u16KeyMask)
{
    uint8_t  Index;
    uint16_t u16KeyBit;

    for (Index = 0u; Index < (uint8_t)E_BSP_KEY_NUM; Index++)
    {
        u16KeyBit = (uint16_t)(1u << Index);
        if ((u16KeyMask & u16KeyBit) == 0u)
        {
            continue;                                       /* 该键不属于本注册项 */
        }
        if (BspGpioGetButtonLevel((eBspButtonIdDef)u16KeyBit) != DEV_BTN_ACTIVE_LEVEL)
        {
            return (uint8_t)(!DEV_BTN_ACTIVE_LEVEL);        /* 有一个键没按下：整项未按下 */
        }
    }
    return DEV_BTN_ACTIVE_LEVEL;
}

/**
 * @brief  单条目注册项的消抖与状态机推进。
 * @param[in,out] ptButton 条目上下文。
 * @param[in] u16KeyMask 注册项的键值掩码，用于读电平与出事件。
 */
static void DevButtonHandler(tDevButtonDef *ptButton, uint16_t u16KeyMask)
{
    uint8_t u8Read = DevButtonReadLevel(u16KeyMask);   /* 1=未按下, 0=按下 */

    if (ptButton->u8State > (uint8_t)E_DEV_BTN_STATE_IDLE) { ptButton->u16Ticks++; }
    if (ptButton->u8HoldDiv > 0u)                          { ptButton->u8HoldDiv--; }

    /* 消抖：连续读到同一新电平达到 DEV_BTN_DEBOUNCE_NUM 次才认可 */
    if (u8Read != ptButton->u8Level)
    {
        if (++ptButton->u8Debounce >= (uint8_t)DEV_BTN_DEBOUNCE_NUM)
        {
            ptButton->u8Level = u8Read;
            ptButton->u8Debounce = 0u;
        }
    }
    else
    {
        ptButton->u8Debounce = 0u;
    }

    switch (ptButton->u8State)
    {
    case E_DEV_BTN_STATE_IDLE:
        if (ptButton->u8Level == DEV_BTN_ACTIVE_LEVEL)
        {
            DevButtonQueueEvent(u16KeyMask, E_DEV_BTN_DOWN);
            ptButton->u16Ticks = 0u;
            ptButton->u8Repeat = 1u;
            ptButton->u8Suppress = 0u;                              /* 新一次点按序列 */
            ptButton->u8State = (uint8_t)E_DEV_BTN_STATE_PRESS;
        }
        break;

    case E_DEV_BTN_STATE_PRESS:
        if (ptButton->u8Level != DEV_BTN_ACTIVE_LEVEL)
        {
            DevButtonQueueEvent(u16KeyMask, E_DEV_BTN_UP);
            ptButton->u16Ticks = 0u;
            ptButton->u8State = (uint8_t)E_DEV_BTN_STATE_RELEASE;
        }
        else if (ptButton->u16Ticks > (uint16_t)DEV_BTN_LONG_TICKS)
        {
            DevButtonQueueEvent(u16KeyMask, E_DEV_BTN_LONG_PRESS);
            ptButton->u8HoldDiv = 0u;
            ptButton->u8State = (uint8_t)E_DEV_BTN_STATE_LONG_HOLD;
        }
        break;

    case E_DEV_BTN_STATE_RELEASE:
        if (ptButton->u8Level == DEV_BTN_ACTIVE_LEVEL)              /* 多击窗口内又按下 */
        {
            DevButtonQueueEvent(u16KeyMask, E_DEV_BTN_DOWN);
            if (ptButton->u8Repeat < (uint8_t)DEV_BTN_REPEAT_MAX) { ptButton->u8Repeat++; }
            DevButtonQueueEvent(u16KeyMask, E_DEV_BTN_REPEAT);
            ptButton->u16Ticks = 0u;
            ptButton->u8State = (uint8_t)E_DEV_BTN_STATE_REPEAT;
        }
        else if (ptButton->u16Ticks > (uint16_t)DEV_BTN_CLICK_WINDOW)  /* 窗口超时 */
        {
            if (ptButton->u8Suppress != 0u)
            {
                ptButton->u8Suppress = 0u;                          /* 组合成立：本轮不出结果 */
            }
            else if (ptButton->u8Repeat == 1u) { DevButtonQueueEvent(u16KeyMask, E_DEV_BTN_SINGLE_CLICK); }
            else if (ptButton->u8Repeat == 2u) { DevButtonQueueEvent(u16KeyMask, E_DEV_BTN_DOUBLE_CLICK); }
            ptButton->u8State = (uint8_t)E_DEV_BTN_STATE_IDLE;
        }
        break;

    case E_DEV_BTN_STATE_REPEAT:
        if (ptButton->u8Level != DEV_BTN_ACTIVE_LEVEL)
        {
            DevButtonQueueEvent(u16KeyMask, E_DEV_BTN_UP);
            if (ptButton->u16Ticks < (uint16_t)DEV_BTN_CLICK_WINDOW)
            {
                ptButton->u16Ticks = 0u;
                ptButton->u8State = (uint8_t)E_DEV_BTN_STATE_RELEASE;   /* 继续等后续点按 */
            }
            else
            {
                ptButton->u8State = (uint8_t)E_DEV_BTN_STATE_IDLE;      /* 连击序列结束 */
            }
        }
        else if (ptButton->u16Ticks > (uint16_t)DEV_BTN_CLICK_WINDOW)
        {
            ptButton->u8State = (uint8_t)E_DEV_BTN_STATE_PRESS;         /* 按太久，转普通按下 */
        }
        break;

    case E_DEV_BTN_STATE_LONG_HOLD:
        if (ptButton->u8Level == DEV_BTN_ACTIVE_LEVEL)
        {
            if (ptButton->u8HoldDiv == 0u)                              /* 100ms 节流，避免事件洪水 */
            {
                DevButtonQueueEvent(u16KeyMask, E_DEV_BTN_LONG_HOLD);
                ptButton->u8HoldDiv = (uint8_t)DEV_BTN_HOLD_DIV;
            }
        }
        else
        {
            DevButtonQueueEvent(u16KeyMask, E_DEV_BTN_UP);
            ptButton->u8State = (uint8_t)E_DEV_BTN_STATE_IDLE;
        }
        break;

    default:
        ptButton->u8State = (uint8_t)E_DEV_BTN_STATE_IDLE;
        break;
    }
}

/**
 * @brief  组合压制：组合项按住期间，与它同时按住的成员项本轮不出单击/双击结果。
 * @note   在本轮所有状态机跑完之后，按当前（已消抖）电平判定。压制标志由被压制的
 *         条目在自己的点按序列出结果时消费，所以一次"同时按下"只会出组合项的结果。
 */
static void DevButtonUpdateSuppress(void)
{
    uint8_t Index;
    uint8_t Combo;

    for (Index = 0u; Index < s_keyNum; Index++)
    {
        if (s_atButton[Index].u8Level != DEV_BTN_ACTIVE_LEVEL)
        {
            continue;                                       /* 本项没按下 */
        }

        for (Combo = 0u; Combo < s_keyNum; Combo++)
        {
            if ((s_aKeyMask[Combo] & (s_aKeyMask[Combo] - 1u)) == 0u)
            {
                continue;                                   /* 不是组合项 */
            }
            if ((s_aKeyMask[Index] == s_aKeyMask[Combo]) ||
                ((s_aKeyMask[Index] & s_aKeyMask[Combo]) != s_aKeyMask[Index]))
            {
                continue;                                   /* 自己 / 该组合项不覆盖本项 */
            }
            if (s_atButton[Combo].u8Level == DEV_BTN_ACTIVE_LEVEL)
            {
                s_atButton[Index].u8Suppress = 1u;          /* 组合成立：本项结果被吃掉 */
                break;
            }
        }
    }
}

/**
 * @brief  1ms 节拍回调：分频到 DEV_BTN_SCAN_MS 后扫描全部按键。
 * @note   由 BspTimeAttachTickCb() 注册，运行在 TIM3 中断上下文。
 */
static void DevButtonTickHandler(void)
{
    static uint8_t  timeCount = 0u;
    uint8_t index;

    if (++timeCount < (uint8_t)DEV_BTN_SCAN_MS)
    {
        return;
    }
    timeCount = 0u;

    for (index = 0u; index < s_keyNum; index++)
    {
        DevButtonHandler(&s_atButton[index], s_aKeyMask[index]);   /* 按注册表逐项扫描 */
    }

    DevButtonUpdateSuppress();                 /* 状态机跑完再按当前电平判组合压制 */
}

/* ========================================================================== *
 *  以下为主循环上下文代码
 * ========================================================================== */

void DevButtonInit(void)
{
    uint8_t Index;

    memset(s_atButton, 0, sizeof(s_atButton));
    memset(s_aKeyMask, 0, sizeof(s_aKeyMask));
    memset(s_atSub, 0, sizeof(s_atSub));
    s_keyNum = 0u;
    s_subNum = 0u;
    s_tQueue.u16Head = 0u;
    s_tQueue.u16Tail = 0u;
    for (Index = 0u; Index < (uint8_t)DEV_BTN_KEY_MAX; Index++)
    {
        /* 初值取有效电平的反值，避免上电瞬间被判成按下 */
        s_atButton[Index].u8Level = (uint8_t)(!DEV_BTN_ACTIVE_LEVEL);
    }

    BspTimeAttachTickCb(DevButtonTickHandler);   /* 注册到 1ms 节拍，须在进入主循环前调用 */
}

/**
 * @brief  注册一条订阅：某键（或键组合）产生某事件时调用 fun。
 * @param[in] u16KeyMask 键值掩码：单键写一个 E_BSP_KEY_x，组合键按位或。
 * @param[in] eEvent 触发事件。
 * @param[in] fun 命中后的回调。
 * @return 1=注册成功；0=掩码非法 / 事件非法 / 回调为空 / 同键同事件重复 / 表已满。
 * @note   须在 DevButtonInit() 之后（Init 会清表）、主循环开始之前注册。
 *         登记订阅的同时把键值记进扫描表（同一掩码只登记一次），注册即扫描；
 *         组合项的电平是掩码内所有键同时按下，出事件时键值就是这个掩码。
 */
uint8_t DevButtonRegister(uint16_t u16KeyMask, eDevButtonEventDef eEvent, FuncPtr fun)
{
    uint8_t index;

    if ((u16KeyMask == 0u) ||
        ((u16KeyMask & (uint16_t)~((1u << (uint8_t)E_BSP_KEY_NUM) - 1u)) != 0u))
    {
        return 0u;                                      /* 掩码非法 */
    }
    if ((eEvent <= E_DEV_BTN_NONE) || (eEvent >= E_DEV_BTN_COUNT) || (fun == NULL))
    {
        return 0u;                                      /* 事件非法或回调为空 */
    }

    for (index = 0u; index < s_subNum; index++)
    {
        if ((s_atSub[index].u16KeyMask == u16KeyMask) &&
            (s_atSub[index].eEvent == eEvent))
        {
            return 0u;                                  /* 同键同事件重复 */
        }
    }
    if (s_subNum >= (uint8_t)DEV_BTN_SUB_MAX)
    {
        return 0u;                                      /* 订阅表已满 */
    }

    for (index = 0u; (index < s_keyNum) && (s_aKeyMask[index] != u16KeyMask); index++)
    {
    }
    if (index >= s_keyNum)                              /* 该键值还没进扫描表 */
    {
        if (s_keyNum >= (uint8_t)DEV_BTN_KEY_MAX)
        {
            return 0u;                                  /* 扫描表已满 */
        }
        s_aKeyMask[s_keyNum] = u16KeyMask;
        s_keyNum++;
    }

    s_atSub[s_subNum].u16KeyMask = u16KeyMask;
    s_atSub[s_subNum].eEvent     = eEvent;
    s_atSub[s_subNum].fun        = fun;
    s_subNum++;

    return 1u;
}

/**
 * @brief  取走并分发队列里所有待处理事件（出队 → 查订阅表 → 调回调）。
 * @note   主循环上下文调用（UsrButtonTask 的 5ms 时间片），是队列的唯一消费者。
 *         事件逐条消费，订阅表里没有对应条目的事件直接丢弃。
 */
void DevButtonProcessEvents(void)
{
    tDevButtonEventDef tEvent;
    uint8_t            index;

    while (s_tQueue.u16Head != s_tQueue.u16Tail)
    {
        tEvent.u16KeyMask = s_tQueue.atItem[s_tQueue.u16Head].u16KeyMask;
        tEvent.eEvent     = s_tQueue.atItem[s_tQueue.u16Head].eEvent;
        s_tQueue.u16Head = (uint16_t)((s_tQueue.u16Head + 1u) % (uint16_t)DEV_BTN_QUEUE_SIZE);

        for (index = 0u; index < s_subNum; index++)
        {
            if ((s_atSub[index].u16KeyMask == tEvent.u16KeyMask) &&
                (s_atSub[index].eEvent == tEvent.eEvent))
            {
                s_atSub[index].fun();
                break;
            }
        }
    }
}
