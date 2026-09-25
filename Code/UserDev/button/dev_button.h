/**
 * @file    dev_button.h
 * @brief   按键设备层：每个注册的按键一份状态机，1ms 节拍分频扫描，事件交主循环分发。
 *
 * @note    本模块属于 Device 层：把 GPIO 电平变成按键事件，并分发给注册的回调。
 *          引脚与电平由 BSP 提供，事件对应什么行为由 Application 通过回调决定。
 *          按键表以"按键掩码"为行：一个注册的按键占一行，该按键的事件回调和它的
 *          状态机计数都放在这一行里，扫描就是逐行推进，不会重复扫描同一个按键。
 *          中断侧逐行采样、消抖、跑状态机，产出的事件压入 SPSC 队列；
 *          主循环侧在时间片内用 DevButtonProcessEvents() 取空队列并按事件查回调。
 */

#ifndef __DEV_BUTTON_H__
#define __DEV_BUTTON_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "UserBsp/bsp_gpio.h"      /* eBspButtonIdDef */

/* 扫描与判定参数（单位 ms） */
#define DEV_BTN_SCAN_MS       (5u)                          /* 扫描间隔 */
#define DEV_BTN_QUEUE_SIZE    (16u)                         /* SPSC 事件队列深度（条目数） */
#define DEV_BTN_MASK_MAX      (1u << (uint8_t)E_BSP_KEY_NUM) /* 可注册的按键掩码数量上限 */
#define DEV_BTN_DEBOUNCE_NUM  (2u)                          /* 消抖采样次数 */
#define DEV_BTN_CLICK_WINDOW  (100u / DEV_BTN_SCAN_MS)      /* 多击判定窗口 */
#define DEV_BTN_LONG_TICKS    (1000u / DEV_BTN_SCAN_MS)     /* 长按判定阈值 */
#define DEV_BTN_REPEAT_MAX    (2u)                          /* 连击计数上限 */
#define DEV_BTN_HOLD_DIV      (100u / DEV_BTN_SCAN_MS)      /* 长按持续上报节流：100ms 一次 */
#define DEV_BTN_ACTIVE_LEVEL   (0u)      /* 本板按键低电平有效 */

/* 按键事件（E_DEV_BTN_NONE 必须为 0） */
typedef enum
{
    E_DEV_BTN_NONE = 0,      /* 无事件 */
    E_DEV_BTN_DOWN,          /* 按下 */
    E_DEV_BTN_UP,            /* 抬起 */
    E_DEV_BTN_REPEAT,        /* 连击中的再次按下 */
    E_DEV_BTN_SINGLE_CLICK,  /* 单击 */
    E_DEV_BTN_DOUBLE_CLICK,  /* 双击 */
    E_DEV_BTN_LONG_PRESS,    /* 长按开始 */
    E_DEV_BTN_LONG_HOLD,     /* 长按持续（每 100ms 上报一次） */
    E_DEV_BTN_COUNT          /* 事件总数（边界标记） */
} eDevButtonEventDef;

/* 事件队列条目：按键掩码 + 事件 id */
typedef struct
{
    uint16_t           buttonMask;   /* 按键掩码（单键或组合的按位或） */
    eDevButtonEventDef event;        /* 事件 */
} tDevButtonEventDef;

/* 只暴露接口；按键表与队列结构体保留在本模块的 .c 内。
 * 掩码每一位对应一个物理按键（E_BSP_KEY_x = 1/2/4），可按位或组成组合掩码；
 * 组合按键按"掩码内所有按键同时按下"判定，出事件时掩码就是这个组合掩码；
 * 组合按键按住期间，被它覆盖的成员按键本轮不出单击/双击结果（组合优先）。
 * 掩码类型为 uint16_t：最多支持 16 个按键位（bit0 到 bit15）。
 * DevButtonRegister() 登记（按键掩码, 事件）→ 回调：同一个按键只占一行、登记即纳入扫描，
 * 同一（按键掩码, 事件）重复注册会被拒绝。 */
void               DevButtonInit(void);
uint8_t            DevButtonRegister(uint16_t buttonMask, eDevButtonEventDef event, FuncPtr fun);
void               DevButtonProcessEvents(void);

#ifdef __cplusplus
}
#endif

#endif /* __DEV_BUTTON_H__ */
