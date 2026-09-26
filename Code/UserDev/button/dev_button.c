/**
 * @file    dev_button.c
 * @brief   按键设备层实现：状态机跑在 1ms 节拍中断内，事件经 SPSC 队列交给主循环。
 *
 * @note    中断路径（DevButtonTickHandler → DevButtonHandler）只做掩码采样、消抖、
 *          状态机与入队。SPSC 队列是唯一的中断到主循环事件通道：单生产者为
 *          TIM3 中断，单消费者为主循环（DevButtonProcessEvents 调用点），
 *          队列元素是按键表行 id（队列条目里不带掩码，也不带事件）。
 *          按键表以"按键掩码"为行：一条（键值, 事件）订阅占一行，该行订阅的事件、
 *          回调与状态机计数都在这一行里；本轮只推进有活动的行，空闲且无关的行不扫。
 *          禁止在中断路径中调用 log/printf、浮点、延时或阻塞等待。
 */

#include "dev_button.h"
#include "UserBsp/bsp_time.h"
#include <string.h>


/* SPSC 队列：中断侧只写 tail，主循环侧只写 head；元素是按键表行 id */
typedef struct
{
    uint16_t ids[DEV_BTN_QUEUE_SIZE];   /* 环形缓冲：只存行 id，掩码和事件都不进队列 */
    uint16_t head;                      /* 消费者游标：主循环独占 */
    uint16_t tail;                      /* 生产者游标：中断独占 */
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

/* 一行 = 一条订阅：（键值掩码, 事件）→ 回调，加上它自己的状态机计数 */
typedef struct
{
    uint16_t keyValue;                     /* 键值（单键或组合的按位或） */
    eDevButtonEventDef event;        /* 本行订阅的事件 */
    FuncPtr  func;   /* 本行订阅事件的回调，NULL=没订阅 */
    uint16_t id;                             /* 本行 id（= 表下标）：入队与分发都用它 */
    uint16_t ticks;                          /* 扫描计数 */
    uint8_t  suppress;                       /* 本轮点按被组合吃掉：不发单击/双击结果 */
    uint8_t  state;                          /* 状态机状态 */
    uint8_t  repeat;                         /* 连击次数 */
    uint8_t  debounce;                       /* 消抖计数 */
    uint8_t  pressed;                        /* 当前（已消抖）状态：1=按下, 0=未按下 */
    uint8_t  holdDiv;                        /* 长按持续上报的节流计数 */
} tDevButtonDef;

/* 按键表：一条订阅一行，行号就是该行的 id，sButtonNum 即当前行数 */
static tDevButtonDef tButton[DEV_BTN_NUM_MAX];
static uint8_t       sButtonNum = 0u;

/* 生命周期标志：0=还没开始扫描（可以清表、可以注册），1=1ms 节拍回调已经挂上。
   它把"清表 → 注册 → 开始扫描"这个顺序变成硬约束：顺序一旦错，断言直接停机，
   而不是静默地不扫描。表只在 sStarted==0 期间改动，所以这里不需要临界区。 */
static uint8_t       sStarted = 0u;


/* ========================================================================== *
 *  以下为中断上下文代码
 * ========================================================================== */

/**
 * @brief  中断侧：把一行 id 压入 SPSC 队列。
 * @param[in] id 按键表行号。
 * @note   只有 TIM3 中断调用，是队列的唯一生产者；队列满时丢弃本次事件，
 *         不覆盖尚未被主循环取走的旧条目。
 */
static void DevButtonPushQueueId(uint16_t id)
{
    uint16_t next;

    next = (uint16_t)((sQueue.tail + 1u) % (uint16_t)DEV_BTN_QUEUE_SIZE);
    if (next == sQueue.head)
    {
        return;                                     /* 队列满：丢弃本次事件 */
    }

    sQueue.ids[sQueue.tail] = id;
    sQueue.tail = next;
}

/**
 * @brief  中断侧：本行订阅了这个事件才把它的行 id 入队。
 * @param[in] button 按键表里的一行。
 * @param[in] event 本轮状态机产出的事件。
 * @note   状态机产出的 DOWN / UP / 连击等事件里，只有本行注册的那一个需要交给主循环，
 *         其余直接丢掉，队列里因此不会出现没人订阅的事件。
 */
static void DevButtonPushEvent(tDevButtonDef *button, eDevButtonEventDef event)
{
    if (button->event != event)
    {
        return;                                     /* 本行没订阅这个事件：不入队 */
    }

    DevButtonPushQueueId(button->id);
}

/**
 * @brief  读当前所有按键的按下情况。
 * @return 按键掩码：置位的 bit 就是当前按下的物理按键（1=按下, 0=未按下）。
 * @note   掩码的每个 bit 就是一个物理按键（E_BSP_KEY_x = 1/2/4），由 BSP 逐位取引脚电平
 *         并完成低电平有效转换，设备层只处理"1=按下"的逻辑值。
 */
static uint16_t DevButtonReadKeyValue(void)
{
    return BspGpioGetButtonMask();
}

/**
 * @brief  组合压制判定：本按键按住期间，是否有覆盖它的组合按键同时成立。
 * @param[in] keyValue 本按键的掩码。
 * @return 1=本按键结果被组合吃掉；0=没有被压制。
 * @note   只在已注册的按键里找组合项，所以没注册过的组合不会有压制作用；
 *         组合项的按下状态用本轮已消抖值（排在后面的行可能还是上一轮的值，差一个扫描周期）。
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
        if (tButton[index].pressed == DEV_BTN_PRESSED)
        {
            return 1u;                                      /* 组合成立：本按键结果被吃掉 */
        }
    }

    return 0u;
}

/**
 * @brief  单个注册按键的消抖与状态机推进。
 * @param[in,out] button 按键表里的一行（键值掩码 + 订阅的事件 + 回调 + 状态机计数）。
 * @param[in] pressedMask 本轮采样到的按下按键掩码（置位 bit = 当前按下的物理按键）。
 * @note   本行按下与否由掩码算出：掩码内所有 bit 都按下才算本行按下，组合按键即"全部按下"，
 *         少一个就是未按下。极性转换在 BSP 完成，本层只认 1=按下。
 */
static void DevButtonHandler(tDevButtonDef *button, uint16_t pressedMask)
{
    uint16_t keyValue = button->keyValue;
    uint8_t  pressed  = 0u;

    if ((keyValue != 0u) && ((pressedMask & keyValue) == keyValue))
    {
        pressed = DEV_BTN_PRESSED;
    }

    if (button->state > (uint8_t)E_DEV_BTN_STATE_IDLE) { button->ticks++; }
    if (button->holdDiv > 0u)                          { button->holdDiv--; }

    /* 消抖：连续读到同一新状态达到 DEV_BTN_DEBOUNCE_NUM 次才认可 */
    if (pressed != button->pressed)
    {
        if (++button->debounce >= (uint8_t)DEV_BTN_DEBOUNCE_NUM)
        {
            button->pressed = pressed;
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
        if (button->pressed == DEV_BTN_PRESSED)
        {
            DevButtonPushEvent(button, E_DEV_BTN_DOWN);
            button->ticks = 0u;
            button->repeat = 1u;
            button->suppress = 0u;                                  /* 新一次点按序列 */
            button->state = (uint8_t)E_DEV_BTN_STATE_PRESS;
        }
        break;

    case E_DEV_BTN_STATE_PRESS:
        if (button->pressed != DEV_BTN_PRESSED)
        {
            DevButtonPushEvent(button, E_DEV_BTN_UP);
            button->ticks = 0u;
            button->state = (uint8_t)E_DEV_BTN_STATE_RELEASE;
        }
        else if (button->ticks > (uint16_t)DEV_BTN_LONG_TICKS)
        {
            DevButtonPushEvent(button, E_DEV_BTN_LONG_PRESS);
            button->holdDiv = 0u;
            button->state = (uint8_t)E_DEV_BTN_STATE_LONG_HOLD;
        }
        break;

    case E_DEV_BTN_STATE_RELEASE:
        if (button->pressed == DEV_BTN_PRESSED)                  /* 多击窗口内又按下 */
        {
            DevButtonPushEvent(button, E_DEV_BTN_DOWN);
            if (button->repeat < (uint8_t)DEV_BTN_REPEAT_MAX) { button->repeat++; }
            DevButtonPushEvent(button, E_DEV_BTN_REPEAT);
            button->ticks = 0u;
            button->state = (uint8_t)E_DEV_BTN_STATE_REPEAT;
        }
        else if (button->ticks > (uint16_t)DEV_BTN_CLICK_WINDOW)    /* 窗口超时 */
        {
            if (button->suppress != 0u)
            {
                button->suppress = 0u;                              /* 组合成立：本轮不出结果 */
            }
            else if (button->repeat == 1u) { DevButtonPushEvent(button, E_DEV_BTN_SINGLE_CLICK); }
            else if (button->repeat == 2u) { DevButtonPushEvent(button, E_DEV_BTN_DOUBLE_CLICK); }
            button->state = (uint8_t)E_DEV_BTN_STATE_IDLE;
        }
        break;

    case E_DEV_BTN_STATE_REPEAT:
        if (button->pressed != DEV_BTN_PRESSED)
        {
            DevButtonPushEvent(button, E_DEV_BTN_UP);
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
        if (button->pressed == DEV_BTN_PRESSED)
        {
            if (button->holdDiv == 0u)                              /* 100ms 节流，避免事件洪水 */
            {
                DevButtonPushEvent(button, E_DEV_BTN_LONG_HOLD);
                button->holdDiv = (uint8_t)DEV_BTN_HOLD_DIV;
            }
        }
        else
        {
            DevButtonPushEvent(button, E_DEV_BTN_UP);
            button->state = (uint8_t)E_DEV_BTN_STATE_IDLE;
        }
        break;

    default:
        button->state = (uint8_t)E_DEV_BTN_STATE_IDLE;
        break;
    }

    /* 组合压制：本按键按住期间若有覆盖它的组合项同时成立，本轮点按结果作废 */
    if ((button->pressed == DEV_BTN_PRESSED) &&
        (DevButtonIsComboSuppressed(keyValue) != 0u))
    {
        button->suppress = 1u;
    }
}

/**
 * @brief  1ms 节拍回调：分频到 DEV_BTN_SCAN_MS 后扫描按键表。
 * @note   由 BspTimeAttachTickCb() 注册，运行在 TIM3 中断上下文。
 *         本轮只推进"有活动"的行：键值掩码与本行有交集，或者本行状态机还没回到空闲。
 *         后半个条件不能省——松手以后键值里就没有本行的位了，若不继续推进，
 *         抬起、多击窗口超时、连击计时全都没人推。
 *         空闲且键值无交集的行本轮一次都不动。
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

    /* 本轮的按键掩码只取一次，所有行共用这一份采样 */
    keyValue = DevButtonReadKeyValue();

    for (index = 0u; index < sButtonNum; index++)
    {
        if (((keyValue & tButton[index].keyValue) == 0u) &&
            (tButton[index].state == (uint8_t)E_DEV_BTN_STATE_IDLE))
        {
            continue;                                   /* 本行没活动：本轮不扫 */
        }

        DevButtonHandler(&tButton[index], keyValue);
    }
}

/* ========================================================================== *
 *  以下为主循环上下文代码
 * ========================================================================== */

void DevButtonInit(void)
{
    /* 顺序检查，两条都为的是把"清表必须排在注册和扫描之前"这一步钉死：
       - 表里已经有行 = 先注册后 Init，这次清表会把刚注册的订阅全吃掉；
       - 已经开扫 = 中断正在扫这张表，边扫边清会读到半成品行。 */
    ASSERT(sButtonNum == 0u);
    ASSERT(sStarted == 0u);

    memset(tButton, 0, sizeof(tButton));
    sButtonNum = 0u;
    sQueue.head = 0u;
    sQueue.tail = 0u;
    sStarted = 0u;
}


void DevButtonStart(void)
{
    ASSERT(sStarted == 0u);                      /* 只能启动一次 */

    BspTimeAttachTickCb(DevButtonTickHandler);   /* 注册到 1ms 节拍，须在进入主循环前调用 */
    sStarted = 1u;
}

/**
 * @brief  注册一条订阅：某按键（单键或组合）产生某事件时调用 fun。
 * @param[in] keyValue 按键掩码：单键写一个 E_BSP_KEY_x，组合按键按位或。
 * @param[in] event 触发事件。
 * @param[in] fun 命中后的回调。
 * @return 1=注册成功（同一（键值, 事件）重复注册＝覆盖回调）；0=掩码非法 / 事件非法 /
 *         回调为空 / 按键表已满。
 * @note   调用顺序固定为 DevButtonInit() → DevButtonRegister() → DevButtonStart()。
 *         在 DevButtonStart() 之后注册会触发断言：那时中断已经在扫这张表，
 *         边扫边改会读到"行数已加、字段没写完"的行。顺序由 sStarted 强制，
 *         所以注册路径里不需要临界区。
 *         一条（键值, 事件）订阅占表里一行，重复注册同一组合只覆盖那一行的回调，
 *         不占新行，行号也不变；登记即纳入扫描。
 *         组合按键要求掩码内所有按键同时按下，产生的事件里带的就是这个组合掩码。
 */
uint8_t DevButtonRegister(uint16_t keyValue, eDevButtonEventDef event, FuncPtr fun)
{
    uint8_t index;

    if ((keyValue == 0u) ||
        ((keyValue & (uint16_t)~((1u << (uint8_t)E_BSP_KEY_NUM) - 1u)) != 0u))
    {
        return 0u;                                      /* 掩码非法 */
    }
    if ((event <= E_DEV_BTN_NONE) || (event >= E_DEV_BTN_COUNT) || (fun == NULL))
    {
        return 0u;                                      /* 事件非法或回调为空 */
    }

    /* 已经开扫还来注册：顺序错了，断言停机（这也是不加临界区的前提） */
    ASSERT(sStarted == 0u);

    /* 查找（键值, 事件）是否已经登记过 */
    for (index = 0u; index < sButtonNum; index++)
    {
        if ((tButton[index].keyValue != keyValue) || (tButton[index].event != event))
        {
            continue;                                   /* 不是这条订阅 */
        }
        /* 已登记过：只换回调，行号不变，状态机状态保留 */
        tButton[index].func = fun;
        return 1u;
    }

    /* 新订阅：占表尾一行（同一个键的不同事件也是另外一行） */
    if (sButtonNum >= DEV_BTN_NUM_MAX)
    {
        ASSERT(sButtonNum < DEV_BTN_NUM_MAX);   /* 表满＝配置错误，不该发生 */
        return 0u;                                      /* 按键表已满 */
    }

    tButton[sButtonNum].id = sButtonNum;        /* 行号即 id，出队时按它取回调 */
    tButton[sButtonNum].keyValue = keyValue;
    tButton[sButtonNum].event = event;
    tButton[sButtonNum].func = fun;             /* 这个按键出这个事件时调 fun */
    sButtonNum++;
    return 1u;
}

/**
 * @brief  取走并分发队列里所有待处理事件（出队 → 用行 id 直接取那一行的回调）。
 * @note   主循环上下文调用（UsrButtonTask 的 5ms 时间片），是队列的唯一消费者。
 *         入队时已经按"本行订不订阅这个事件"过滤过，这里只需按 id 调回调；
 *         id 越界（表被清过）的条目直接丢弃，不用再去比对键值。
 */
void DevButtonProcessEvents(void)
{
    uint16_t id;

    while (sQueue.head != sQueue.tail)
    {
        id = sQueue.ids[sQueue.head];
        sQueue.head = (uint16_t)((sQueue.head + 1u) % (uint16_t)DEV_BTN_QUEUE_SIZE);

        if (id >= sButtonNum)
        {
            continue;                                       /* 这一行已经不在了 */
        }
        if (tButton[id].func != NULL)
        {
            tButton[id].func();                             /* 本行订阅的事件到了 */
        }
    }
}
