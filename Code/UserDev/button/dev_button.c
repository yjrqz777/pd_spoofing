/**
 * @file    dev_button.c
 * @brief   按键设备层实现：状态机跑在 1ms 节拍中断内，事件经 SPSC 队列交给主循环。
 *
 * @note    中断路径（DevButtonTickHandler → DevButtonHandler）只做电平采样、消抖、
 *          状态机与入队。SPSC 队列是唯一的中断到主循环事件通道：单生产者为
 *          TIM3 中断，单消费者为主循环（DevButtonProcessEvents 调用点）。
 *          按键表以"按键掩码"为行：一个注册的按键一行，该按键的事件回调与状态机
 *          计数都在这一行里，扫描就是逐行推进，结构上不会重复扫描同一个按键。
 *          禁止在中断路径中调用 log/printf、浮点、延时或阻塞等待。
 */

#include "dev_button.h"
#include "UserBsp/bsp_time.h"
#include <string.h>


/* SPSC 队列：中断侧只写 tail，主循环侧只写 head */
typedef struct
{
    eDevButtonEventDef events[DEV_BTN_QUEUE_SIZE];   /* 环形缓冲：按键掩码 + 事件 */
    uint16_t           head;                        /* 头 消费者游标：主循环独占 */
    uint16_t           tail;                        /* 尾 生产者游标：中断独占 */
} tDevButtonQueueDef;

static volatile tDevButtonQueueDef sQueue;

/* 按键状态机状态（本文件私有） */
typedef enum
{
    E_DEV_BTN_STATE_IDLE = 0,    /* 空闲 */
    E_DEV_BTN_STATE_PRESS,       /* 已按下 */
    E_DEV_BTN_STATE_RELEASE,     /* 已抬起，等待多击超时 */
    E_DEV_BTN_STATE_REPEAT,      /* 连击中的再次按下 */
    E_DEV_BTN_STATE_LONG_HOLD    /* 长按保持 */
} eDevButtonStateDef;

/* 一行 = 一个注册的按键：按键掩码 + 该按键的事件回调表 + 它自己的状态机计数 */
typedef struct
{
    uint16_t keyValue;                     /* 键值（单键或组合的按位或） */
    eDevButtonEventDef event;        /* 事件 */
    FuncPtr  func;   /* 事件 id → 回调，NULL=该事件没订阅 */
    uint16_t ticks;                          /* 扫描计数 */
    uint8_t  suppress;                       /* 本轮点按被组合吃掉：不发单击/双击结果 */
    uint8_t  state;                          /* 状态机状态 */
    uint8_t  repeat;                         /* 连击次数 */
    uint8_t  debounce;                       /* 消抖计数 */
    uint8_t  level;                          /* 当前（已消抖）电平：1=未按下, 0=按下 */
    uint8_t  holdDiv;                        /* 长按持续上报的节流计数 */
} tDevButtonDef;

/* 按键表：一个注册的按键一行，sButtonNum 即当前行数（也就是要扫描的按键个数） */
static tDevButtonDef tButton[DEV_BTN_NUM_MAX];
static uint8_t       sButtonNum = 0u;


/* ========================================================================== *
 *  以下为中断上下文代码
 * ========================================================================== */

/**
 * @brief  中断侧：把（按键掩码, 事件 id）压入 SPSC 队列。
 * @param[in] keyValue 按键掩码（单键或组合）。
 * @param[in] event 事件。
 * @note   只有 TIM3 中断调用，是队列的唯一生产者。状态机产出什么事件就记录什么事件，
 *         有没有人订阅与入队无关；队列满时丢弃本次事件，不覆盖尚未被主循环取走的旧事件。
 */
static void DevButtonPushQueueEvent(uint16_t keyValue, eDevButtonEventDef event)
{
    uint16_t next;

    if ((event <= E_DEV_BTN_NONE) || (event >= E_DEV_BTN_COUNT))
    {
        return;
    }

    next = (uint16_t)((sQueue.tail + 1u) % (uint16_t)DEV_BTN_QUEUE_SIZE);
    if (next == sQueue.head)
    {
        return;                                     /* 队列满：丢弃本次事件 */
    }

    sQueue.events[sQueue.tail]      = event;
    sQueue.tail = next;
}

/**
 * @brief  读一个按键掩码的电平：掩码内所有按键同时为有效电平才算按下。
 * @param[in] keyValue 按键掩码（单键或组合）。
 * @return 1=未按下, 0=按下。
 * @note   掩码的每个 bit 就是一个物理按键（E_BSP_KEY_x = 1/2/4），逐位取引脚电平；
 *         组合按键按"全部按下"合成一个逻辑按键，少一个就整项未按下。
 */
static uint16_t DevButtonReadKeyValue()
{
    // uint8_t  index;
    // uint16_t buttonBit;

    return BspGpioGetButtonLevel(E_BSP_KEY_MAX);

}

/**
 * @brief  组合压制判定：本按键按住期间，是否有覆盖它的组合按键同时成立。
 * @param[in] keyValue 本按键的掩码。
 * @return 1=本按键结果被组合吃掉；0=没有被压制。
 * @note   只在已注册的按键里找组合项，所以没注册过的组合不会有压制作用；
 *         组合项的电平用本轮已消抖值（排在后面的行可能还是上一轮的值，差一个扫描周期）。
 */
static uint8_t DevButtonIsComboSuppressed(uint16_t keyValue)
{
    uint8_t  index;
    uint16_t combo;

    for (index = 0u; index < sButtonNum; index++)
    {
        combo = tButton[index].keyValue;
        if ((combo & (combo - 1u)) == 0u)
        {
            continue;                                       /* 不是组合项 */
        }
        if ((combo == keyValue) || ((keyValue & combo) != keyValue))
        {
            continue;                                       /* 自己 / 该组合项不覆盖本按键 */
        }
        if (tButton[index].level == DEV_BTN_ACTIVE_LEVEL)
        {
            return 1u;                                      /* 组合成立：本按键结果被吃掉 */
        }
    }

    return 0u;
}

/**
 * @brief  单个注册按键的消抖与状态机推进。
 * @param[in,out] button 按键表里的一行（掩码 + 回调表 + 状态机计数）。
 * @param[in] keyValue 读取到的按键电平。
 */
static void DevButtonHandler(tDevButtonDef *button)
{
    uint16_t keyValue = button->keyValue;

    if (button->state > (uint8_t)E_DEV_BTN_STATE_IDLE) { button->ticks++; }
    if (button->holdDiv > 0u)                          { button->holdDiv--; }

    /* 消抖：连续读到同一新电平达到 DEV_BTN_DEBOUNCE_NUM 次才认可 */
    if (keyValue != button->level)
    {
        if (++button->debounce >= (uint8_t)DEV_BTN_DEBOUNCE_NUM)
        {
            button->level = keyValue;
            button->debounce = 0u;
        }
    }
    else
    {
        button->debounce = 0u;
    }

    switch (button->state)
    {
    case E_DEV_BTN_STATE_IDLE:
        if (button->level == DEV_BTN_ACTIVE_LEVEL)
        {
            DevButtonPushQueueEvent(keyValue, E_DEV_BTN_DOWN);
            button->ticks = 0u;
            button->repeat = 1u;
            button->suppress = 0u;                                  /* 新一次点按序列 */
            button->state = (uint8_t)E_DEV_BTN_STATE_PRESS;
        }
        break;

    case E_DEV_BTN_STATE_PRESS:
        if (button->level != DEV_BTN_ACTIVE_LEVEL)
        {
            DevButtonPushQueueEvent(keyValue, E_DEV_BTN_UP);
            button->ticks = 0u;
            button->state = (uint8_t)E_DEV_BTN_STATE_RELEASE;
        }
        else if (button->ticks > (uint16_t)DEV_BTN_LONG_TICKS)
        {
            DevButtonPushQueueEvent(keyValue, E_DEV_BTN_LONG_PRESS);
            button->holdDiv = 0u;
            button->state = (uint8_t)E_DEV_BTN_STATE_LONG_HOLD;
        }
        break;

    case E_DEV_BTN_STATE_RELEASE:
        if (button->level == DEV_BTN_ACTIVE_LEVEL)                  /* 多击窗口内又按下 */
        {
            DevButtonPushQueueEvent(keyValue, E_DEV_BTN_DOWN);
            if (button->repeat < (uint8_t)DEV_BTN_REPEAT_MAX) { button->repeat++; }
            DevButtonPushQueueEvent(keyValue, E_DEV_BTN_REPEAT);
            button->ticks = 0u;
            button->state = (uint8_t)E_DEV_BTN_STATE_REPEAT;
        }
        else if (button->ticks > (uint16_t)DEV_BTN_CLICK_WINDOW)    /* 窗口超时 */
        {
            if (button->suppress != 0u)
            {
                button->suppress = 0u;                              /* 组合成立：本轮不出结果 */
            }
            else if (button->repeat == 1u) { DevButtonPushQueueEvent(keyValue, E_DEV_BTN_SINGLE_CLICK); }
            else if (button->repeat == 2u) { DevButtonPushQueueEvent(keyValue, E_DEV_BTN_DOUBLE_CLICK); }
            button->state = (uint8_t)E_DEV_BTN_STATE_IDLE;
        }
        break;

    case E_DEV_BTN_STATE_REPEAT:
        if (button->level != DEV_BTN_ACTIVE_LEVEL)
        {
            DevButtonPushQueueEvent(keyValue, E_DEV_BTN_UP);
            if (button->ticks < (uint16_t)DEV_BTN_CLICK_WINDOW)
            {
                button->ticks = 0u;
                button->state = (uint8_t)E_DEV_BTN_STATE_RELEASE;   /* 继续等后续点按 */
            }
            else
            {
                button->state = (uint8_t)E_DEV_BTN_STATE_IDLE;      /* 连击序列结束 */
            }
        }
        else if (button->ticks > (uint16_t)DEV_BTN_CLICK_WINDOW)
        {
            button->state = (uint8_t)E_DEV_BTN_STATE_PRESS;         /* 按太久，转普通按下 */
        }
        break;

    case E_DEV_BTN_STATE_LONG_HOLD:
        if (button->level == DEV_BTN_ACTIVE_LEVEL)
        {
            if (button->holdDiv == 0u)                              /* 100ms 节流，避免事件洪水 */
            {
                DevButtonPushQueueEvent(keyValue, E_DEV_BTN_LONG_HOLD);
                button->holdDiv = (uint8_t)DEV_BTN_HOLD_DIV;
            }
        }
        else
        {
            DevButtonPushQueueEvent(keyValue, E_DEV_BTN_UP);
            button->state = (uint8_t)E_DEV_BTN_STATE_IDLE;
        }
        break;

    default:
        button->state = (uint8_t)E_DEV_BTN_STATE_IDLE;
        break;
    }

    /* 组合压制：本按键按住期间若有覆盖它的组合项同时成立，本轮点按结果作废 */
    if ((button->level == DEV_BTN_ACTIVE_LEVEL) &&
        (DevButtonIsComboSuppressed(keyValue) != 0u))
    {
        button->suppress = 1u;
    }
}

/**
 * @brief  1ms 节拍回调：分频到 DEV_BTN_SCAN_MS 后扫描按键表。
 * @note   由 BspTimeAttachTickCb() 注册，运行在 TIM3 中断上下文。
 *         一个注册的按键一行，逐行推进即可，同一个按键不会被扫两次。
 */
static void DevButtonTickHandler(void)
{
    static uint8_t timeCount = 0u;
    uint8_t index = 0;
    uint16_t keyValue = 0u;

    if (++timeCount < (uint8_t)DEV_BTN_SCAN_MS)
    {
        return;
    }
    timeCount = 0u;


    /*先提取键值，依据键值扫描，并不每次扫描全部注册按键*/
    keyValue = DevButtonReadKeyValue();


    for (index = 0u; index < sButtonNum; index++)
    {
        if (tButton[index].keyValue == keyValue)/* 找到对应的按键  */
        {
            DevButtonHandler(&tButton[index]); /* 扫描 键值 */
        }
    }
}

/* ========================================================================== *
 *  以下为主循环上下文代码
 * ========================================================================== */

void DevButtonInit(void)
{
    // memset(tButton, 0, sizeof(tButton));
    sButtonNum = 0u;
    sQueue.head = 0u;
    sQueue.tail = 0u;

    BspTimeAttachTickCb(DevButtonTickHandler);   /* 注册到 1ms 节拍，须在进入主循环前调用 */
}

/**
 * @brief  注册一条订阅：某按键（单键或组合）产生某事件时调用 fun。
 * @param[in] keyValue 按键掩码：单键写一个 E_BSP_KEY_x，组合按键按位或。
 * @param[in] event 触发事件。
 * @param[in] fun 命中后的回调。
 * @return 1=注册成功；0=掩码非法 / 事件非法 / 回调为空 / 同键同事件重复 / 按键表已满。
 * @note   须在 DevButtonInit() 之后（Init 会清表）、主循环开始之前注册。
 *         新按键会在表里占一行，已有按键直接用它那一行；登记即纳入扫描。
 *         组合按键的电平是掩码内所有按键同时按下，出事件时掩码就是这个组合掩码。
 */
uint8_t DevButtonRegister(uint16_t Mask, eDevButtonEventDef event, FuncPtr fun)
{
    uint8_t index;

    if ((Mask == 0u) ||
        ((Mask & (uint16_t)~((1u << (uint8_t)E_BSP_KEY_MAX) - 1u)) != 0u))
    {
        return 0u;                                      /* 掩码非法 */
    }
    if ((event <= E_DEV_BTN_NONE) || (event >= E_DEV_BTN_COUNT) || (fun == NULL))
    {
        return 0u;                                      /* 事件非法或回调为空 */
    }

    if (sButtonNum >= DEV_BTN_NUM_MAX)
    {
        #error "key table full, please increase DEV_BTN_NUM_MAX"
        return 0u;                                      /* 按键表已满 */
    }
    


    
    for (index = 0u; index < sButtonNum; index++)
    {
        if (tButton[index].keyValue != Mask)
        {
            continue;                                   /* 不是这个按键 */
        }
        /* 键值相同 事件不同 记录在下一个空间*/
        if (tButton[index].event != event)
        {
            sButtonNum++; 
            break;
        }
        /* 键值相同 事件相同 覆盖空间*/
        if (tButton[index].event == event)
        {
            sButtonNum = index; /* 覆盖空间*/
            break; 
        }
    }
    

    tButton[sButtonNum].keyValue = Mask;
    tButton[sButtonNum].event = event;
    tButton[sButtonNum].func = fun;             /* 这个按键出这个事件时调 fun */
    sButtonNum++;
    return 1u;
}

/**
 * @brief  取走并分发队列里所有待处理事件（出队 → 找到该按键那一行 → 查事件回调）。
 * @note   主循环上下文调用（UsrButtonTask 的 5ms 时间片），是队列的唯一消费者。
 *         事件逐条消费，该按键没订阅这个事件就丢弃。
 */
void DevButtonProcessEvents(void)
{
    uint16_t           keyValue;
    eDevButtonEventDef event;
    uint8_t            index;

    while (sQueue.head != sQueue.tail)
    {
        event      = sQueue.events[sQueue.head];
        sQueue.head = (uint16_t)((sQueue.head + 1u) % (uint16_t)DEV_BTN_QUEUE_SIZE);

        for (index = 0u; index < sButtonNum; index++)
        {
            if (tButton[index].keyValue != keyValue)
            {
                continue;                               /* 不是这个按键 */
            }
            if (tButton[index].func != NULL)
            {
                tButton[index].func();     /* 这个按键订阅了这个事件 */
            }
            break;
        }
    }
}
