/**
 * @file dev_button.c
 * @brief   按键设备层实现：状态机跑在 1ms 节拍中断内，事件经 SPSC 队列交给主循环。
 *
 * @note    中断路径（DevButtonTickHandler → DevButtonHandler）只做掩码采样、消抖、
 *          状态机与入队。SPSC 队列是唯一的中断到主循环事件通道：单生产者为
 *          TIM3 中断，单消费者为主循环（DevButtonProcessEvents 调用点），
 *          队列元素是按键表行 id（队列条目里不带掩码，也不带事件）。
 *          按键表以"按键掩码"为行：一条（键值, 事件）订阅占一行，该行订阅的事件、
 *          回调与状态机计数都在这一行里；本轮只推进有活动的行，空闲且无关的行不扫。
 *          禁止在中断路径中调用 log/printf、浮点、延时或阻塞等待。
 
 * @author 段志炫
 * @date 2026-10-15
 * @version 0.1.1
 * @par 修改记录
 * 2026-10-15 按项目注释规范补齐文件和接口说明。*/

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
    E_DEV_BTN_STATE_IDLE = 0,    /**< 空闲 */
    E_DEV_BTN_STATE_PRESS,       /**< 已按下 */
    E_DEV_BTN_STATE_RELEASE,     /**< 已抬起，等待多击超时 */
    E_DEV_BTN_STATE_REPEAT,      /**< 连击中的再次按下 */
    E_DEV_BTN_STATE_LONG_HOLD    /**< 长按保持 */
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
 * @brief 将按键表行号写入事件队列
 * @param[in] id 按键表行号，单位：行，范围由按键订阅表容量限定。
 * @return 函数执行结果或查询值，取值含义见函数说明。
 * @note 调用前确认参数有效，并在模块规定的上下文中调用。
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
 * @brief 将已订阅的按键事件加入事件队列
 * @param[out] button 按键订阅状态指针，包含订阅事件、状态机计数和行号。
 * @param[in] event 按键事件类型，取值为按键事件枚举成员。
 * @return 函数执行结果或查询值，取值含义见函数说明。
 * @note 调用前确认参数有效，并在模块规定的上下文中调用。
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
 * @brief 读取当前按键掩码
 * @param[in] 无 无输入参数。
 * @return 当前任务或查询结果，单位和取值含义见函数说明。
 * @note 调用前确认参数有效，并在模块规定的上下文中调用。
 */
static uint16_t DevButtonReadKeyValue(void)
{
    return BspGpioGetButtonMask();
}

/**
 * @brief 判断组合按键是否抑制单键事件
 * @param[in] keyValue 按键掩码，使用 BSP 定义的按键位掩码。
 * @return 函数执行结果或查询值，取值含义见函数说明。
 * @note 调用前确认参数有效，并在模块规定的上下文中调用。
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
 * @brief 推进单条按键订阅的消抖与状态机
 * @param[out] button 按键订阅状态指针，包含订阅事件、状态机计数和行号。
 * @param[in] pressedMask 当前采样得到的按下键掩码，位定义与 keyValue 相同。
 * @return 函数执行结果或查询值，取值含义见函数说明。
 * @note 调用前确认参数有效，并在模块规定的上下文中调用。
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
 * @brief 采样按键并推进订阅状态机
 * @param[in] 无 无输入参数。
 * @return 函数执行结果或查询值，取值含义见函数说明。
 * @note 调用前确认参数有效，并在模块规定的上下文中调用。
 */
ERT(sButtonNum == 0u);
    ASSERT(sStarted == 0u);

    memset(tButton, 0, sizeof(tButton));
    sButtonNum = 0u;
    sQueue.head = 0u;
    sQueue.tail = 0u;
    sStarted = 0u;
}


/**
 * @brief 启动按键周期扫描
 * @param[in] 无 无输入参数。
 * @return 无返回值。
 * @note 必须在所有按键订阅注册完成后调用，且只能调用一次。
 */
void DevButtonStart(void)
{
    ASSERT(sStarted == 0u);                      /* 只能启动一次 */

    BspTimeAttachTickCb(DevButtonTickHandler);   /* 注册到 1ms 节拍，须在进入主循环前调用 */
    sStarted = 1u;
}

/**
 * @brief 注册按键事件及其回调
 * @param[in] keyValue 按键掩码，使用 BSP 定义的按键位掩码。
 * @param[in] event 按键事件类型，取值为按键事件枚举成员。
 * @param[in] fun 按键事件回调函数。
 * @return 函数执行结果或查询值，取值含义见函数说明。
 * @note 必须在 DevButtonInit() 之后、DevButtonStart() 之前调用。
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
 * @brief 取出按键事件并调用对应回调
 * @param[in] 无 无输入参数。
 * @return 无返回值。
 * @note 在主循环上下文周期调用；不要在节拍中断中执行回调。
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


