/**
 * @file    dev_button.c
 * @brief   按键设备层实现：状态机跑在 1ms 节拍中断内，事件由主循环取用。
 *
 * @note    中断路径（DevButtonTickHandler → DevButtonHandler）只做电平采样、
 *          消抖、状态机与位操作。禁止在其中调用 log/printf、浮点、延时或阻塞等待。
 */

#include "dev_button.h"
#include "UserBsp/bsp_time.h"
#include <string.h>

#define DEV_BTN_ACTIVE_LEVEL   (0u)      /* 本板按键低电平有效 */

/* 按键状态机状态（本文件私有） */
typedef enum
{
    E_DEV_BTN_STATE_IDLE = 0,    /* 空闲 */
    E_DEV_BTN_STATE_PRESS,       /* 已按下 */
    E_DEV_BTN_STATE_RELEASE,     /* 已抬起，等待多击超时 */
    E_DEV_BTN_STATE_REPEAT,      /* 连击中的再次按下 */
    E_DEV_BTN_STATE_LONG_HOLD    /* 长按保持 */
} eDevButtonStateDef;

/* 单个按键的上下文（本文件私有，长度由 E_BSP_KEY_NUM 在编译期确定） */
typedef struct
{
    uint16_t          u16Ticks;     /* 扫描计数 */
    volatile uint16_t u16Pending;   /* 待取走事件位掩码：中断内写，主循环读并清 */
    uint8_t           u8State;      /* 状态机状态 */
    uint8_t           u8Repeat;     /* 连击次数 */
    uint8_t           u8Debounce;   /* 消抖计数 */
    uint8_t           u8Level;      /* 当前（已消抖）电平：1=未按下, 0=按下 */
    uint8_t           u8HoldDiv;    /* 长按持续上报的节流计数 */
} tDevButtonDef;

static tDevButtonDef s_atButton[E_BSP_KEY_NUM];
static uint8_t       s_u8ScanDiv = 0u;

/* ========================================================================== *
 *  以下为中断上下文代码
 * ========================================================================== */

/**
 * @brief  把事件登记进待取走掩码。
 * @param[in,out] ptButton 按键上下文。
 * @param[in] eEvent 事件。
 */
static void DevButtonQueueEvent(tDevButtonDef *ptButton, eDevButtonEventDef eEvent)
{
    if (eEvent < E_DEV_BTN_COUNT)
    {
        ptButton->u16Pending |= (uint16_t)(1u << (uint8_t)eEvent);
    }
}

/**
 * @brief  单个按键的消抖与状态机推进。
 * @param[in,out] ptButton 按键上下文。
 * @param[in] eKey 按键编号，用于向 BSP 读电平。
 */
static void DevButtonHandler(tDevButtonDef *ptButton, eBspButtonIdDef eKey)
{
    uint8_t u8Read = BspGpioGetButtonLevel(eKey);      /* 1=未按下, 0=按下 */

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
            DevButtonQueueEvent(ptButton, E_DEV_BTN_DOWN);
            ptButton->u16Ticks = 0u;
            ptButton->u8Repeat = 1u;
            ptButton->u8State = (uint8_t)E_DEV_BTN_STATE_PRESS;
        }
        break;

    case E_DEV_BTN_STATE_PRESS:
        if (ptButton->u8Level != DEV_BTN_ACTIVE_LEVEL)
        {
            DevButtonQueueEvent(ptButton, E_DEV_BTN_UP);
            ptButton->u16Ticks = 0u;
            ptButton->u8State = (uint8_t)E_DEV_BTN_STATE_RELEASE;
        }
        else if (ptButton->u16Ticks > (uint16_t)DEV_BTN_LONG_TICKS)
        {
            DevButtonQueueEvent(ptButton, E_DEV_BTN_LONG_PRESS);
            ptButton->u8HoldDiv = 0u;
            ptButton->u8State = (uint8_t)E_DEV_BTN_STATE_LONG_HOLD;
        }
        break;

    case E_DEV_BTN_STATE_RELEASE:
        if (ptButton->u8Level == DEV_BTN_ACTIVE_LEVEL)              /* 多击窗口内又按下 */
        {
            DevButtonQueueEvent(ptButton, E_DEV_BTN_DOWN);
            if (ptButton->u8Repeat < (uint8_t)DEV_BTN_REPEAT_MAX) { ptButton->u8Repeat++; }
            DevButtonQueueEvent(ptButton, E_DEV_BTN_REPEAT);
            ptButton->u16Ticks = 0u;
            ptButton->u8State = (uint8_t)E_DEV_BTN_STATE_REPEAT;
        }
        else if (ptButton->u16Ticks > (uint16_t)DEV_BTN_CLICK_WINDOW)  /* 窗口超时 */
        {
            if (ptButton->u8Repeat == 1u)      { DevButtonQueueEvent(ptButton, E_DEV_BTN_SINGLE_CLICK); }
            else if (ptButton->u8Repeat == 2u) { DevButtonQueueEvent(ptButton, E_DEV_BTN_DOUBLE_CLICK); }
            ptButton->u8State = (uint8_t)E_DEV_BTN_STATE_IDLE;
        }
        break;

    case E_DEV_BTN_STATE_REPEAT:
        if (ptButton->u8Level != DEV_BTN_ACTIVE_LEVEL)
        {
            DevButtonQueueEvent(ptButton, E_DEV_BTN_UP);
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
                DevButtonQueueEvent(ptButton, E_DEV_BTN_LONG_HOLD);
                ptButton->u8HoldDiv = (uint8_t)DEV_BTN_HOLD_DIV;
            }
        }
        else
        {
            DevButtonQueueEvent(ptButton, E_DEV_BTN_UP);
            ptButton->u8State = (uint8_t)E_DEV_BTN_STATE_IDLE;
        }
        break;

    default:
        ptButton->u8State = (uint8_t)E_DEV_BTN_STATE_IDLE;
        break;
    }
}

/**
 * @brief  1ms 节拍回调：分频到 DEV_BTN_SCAN_MS 后扫描全部按键。
 * @note   由 BspTimeAttachTickHandler() 注册，运行在 TIM3 中断上下文。
 */
static void DevButtonTickHandler(void)
{
    uint8_t Index;

    s_u8ScanDiv++;
    if (s_u8ScanDiv < (uint8_t)DEV_BTN_SCAN_MS)
    {
        return;
    }
    s_u8ScanDiv = 0u;

    for (Index = 0u; Index < (uint8_t)E_BSP_KEY_NUM; Index++)
    {
        DevButtonHandler(&s_atButton[Index], (eBspButtonIdDef)(1u << Index));
    }
}

/* ========================================================================== *
 *  以下为主循环上下文代码
 * ========================================================================== */

void DevButtonInit(void)
{
    uint8_t Index;

    memset(s_atButton, 0, sizeof(s_atButton));
    for (Index = 0u; Index < (uint8_t)E_BSP_KEY_NUM; Index++)
    {
        /* 初值取有效电平的反值，避免上电瞬间被判成按下 */
        s_atButton[Index].u8Level = (uint8_t)(!DEV_BTN_ACTIVE_LEVEL);
    }
    s_u8ScanDiv = 0u;

    BspTimeAttachTickHandler(DevButtonTickHandler);   /* 注册到 1ms 节拍，须在进入主循环前调用 */
}

/**
 * @brief  单键位值转上下文数组下标。
 * @param[in] eKey 单键位值（E_BSP_KEY_x）。
 * @return 数组下标；键值非法（0、多键组合、越界）时返回 E_BSP_KEY_NUM。
 */
static uint8_t DevButtonKeyToIndex(eBspButtonIdDef eKey)
{
    uint8_t Index;

    for (Index = 0u; Index < (uint8_t)E_BSP_KEY_NUM; Index++)
    {
        if ((uint8_t)eKey == (uint8_t)(1u << Index))
        {
            return Index;
        }
    }
    return (uint8_t)E_BSP_KEY_NUM;
}

eDevButtonEventDef DevButtonGetEvent(eBspButtonIdDef eKey)
{
    uint16_t Pending;
    uint8_t  Bit;
    uint8_t  Index = DevButtonKeyToIndex(eKey);

    if (Index >= (uint8_t)E_BSP_KEY_NUM)
    {
        return E_DEV_BTN_NONE;
    }

    BspIrqDisableAll();                       /* 临界区只包住“读 + 清”两步 */
    Pending = s_atButton[Index].u16Pending;
    s_atButton[Index].u16Pending = 0u;
    BspIrqEnableAll();

    if (Pending == 0u)
    {
        return E_DEV_BTN_NONE;
    }

    /* 取最低置位的事件返回；其余事件保留，下一次调用继续取 */
    for (Bit = 0u; Bit < (uint8_t)E_DEV_BTN_COUNT; Bit++)
    {
        if ((Pending & (uint16_t)(1u << Bit)) != 0u)
        {
            break;
        }
    }

    return (eDevButtonEventDef)Bit;
}

uint8_t DevButtonIsPressed(eBspButtonIdDef eKey)
{
    uint8_t Index = DevButtonKeyToIndex(eKey);

    if (Index >= (uint8_t)E_BSP_KEY_NUM)
    {
        return 0u;
    }

    return (s_atButton[Index].u8Level == DEV_BTN_ACTIVE_LEVEL) ? 1u : 0u;
}

/**
 * @brief  查询哪些键有待取的指定事件（查询不消费）。
 * @param[in] eEvent 事件。
 * @return 键位值掩码：置位的 bit n 表示键(n+1)有待取事件；无则返回 0。
 * @note   主循环上下文调用，用于组合键判定：组合条目内所有键的同一事件
 *         同时待取时视为组合命中。
 */
uint8_t DevButtonPendingMask(eDevButtonEventDef eEvent)
{
    uint16_t Bit;
    uint8_t  Mask = 0u;
    uint8_t  Index;

    if ((eEvent <= E_DEV_BTN_NONE) || (eEvent >= E_DEV_BTN_COUNT))
    {
        return 0u;
    }
    Bit = (uint16_t)(1u << (uint8_t)eEvent);

    BspIrqDisableAll();
    for (Index = 0u; Index < (uint8_t)E_BSP_KEY_NUM; Index++)
    {
        if ((s_atButton[Index].u16Pending & Bit) != 0u)
        {
            Mask |= (uint8_t)(1u << Index);
        }
    }
    BspIrqEnableAll();

    return Mask;
}

/**
 * @brief  清除 u8KeyMask 内各键的指定待取事件（消费但不触发）。
 * @param[in] u8KeyMask 键位值掩码，可含多个键。
 * @param[in] eEvent 事件。
 * @note   主循环上下文调用，配合 DevButtonPendingMask：组合键命中后吃掉
 *         各键的单键事件，避免单键条目重复触发。
 */
void DevButtonClearEvent(uint8_t u8KeyMask, eDevButtonEventDef eEvent)
{
    uint16_t Bit;
    uint8_t  Index;

    if ((u8KeyMask == 0u) || (eEvent <= E_DEV_BTN_NONE) || (eEvent >= E_DEV_BTN_COUNT))
    {
        return;
    }
    Bit = (uint16_t)(1u << (uint8_t)eEvent);

    BspIrqDisableAll();
    for (Index = 0u; Index < (uint8_t)E_BSP_KEY_NUM; Index++)
    {
        if ((u8KeyMask & (uint8_t)(1u << Index)) != 0u)
        {
            s_atButton[Index].u16Pending &= (uint16_t)~Bit;
        }
    }
    BspIrqEnableAll();
}
