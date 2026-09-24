/**
 * @file    dev_button.h
 * @brief   按键设备层：在 1ms 节拍中断内分频扫描消抖，事件经 SPSC 队列交给主循环。
 *
 * @note    本模块属于 Device 层：把 GPIO 电平变成按键事件，并分发给注册的回调。
 *          引脚与电平由 BSP 提供，事件对应什么行为由 Application 通过回调决定。
 *          键值掩码与事件订阅都由 DevButtonRegister() 注册（单键或组合键，按位或），
 *          登记订阅的同时把键值记进扫描表；中断侧按扫描表逐项采样，
 *          产出（键值掩码, 事件 id）压入 SPSC 队列；组合项按住时压制成员的单击/双击。
 *          主循环侧在时间片内用 DevButtonProcessEvents() 取空队列并查表回调。
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
#define DEV_BTN_KEY_MAX       (8u)                          /* 可注册的扫描条目上限（单键与组合共用） */
#define DEV_BTN_DEBOUNCE_NUM  (2u)                          /* 消抖采样次数 */
#define DEV_BTN_CLICK_WINDOW  (100u / DEV_BTN_SCAN_MS)      /* 多击判定窗口 */
#define DEV_BTN_LONG_TICKS    (1000u / DEV_BTN_SCAN_MS)     /* 长按判定阈值 */
#define DEV_BTN_REPEAT_MAX    (2u)                          /* 连击计数上限 */
#define DEV_BTN_HOLD_DIV      (100u / DEV_BTN_SCAN_MS)      /* 长按持续上报节流：100ms 一次 */

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

/* 按键事件：键值掩码 + 事件；订阅表条目在此基础上再填 fun。
 * 回调类型用 user_global.h 的 FuncPtr；队列条目不用 fun 字段，恒为 0。 */
typedef struct
{
    uint16_t           u16KeyMask;   /* 注册项的键值掩码（单键或组合的按位或） */
    eDevButtonEventDef eEvent;       /* 事件 */
    FuncPtr            fun;          /* 命中后调用（只有订阅表填） */
} tDevButtonEventDef;

/* 可订阅的（键值掩码, 事件）条目上限 */
#define DEV_BTN_SUB_MAX (8u)

/* 只暴露接口；按键上下文与队列结构体保留在本模块的 .c 内。
 * 键值为位值（E_BSP_KEY_x = 1/2/4），可按位或组成组合键掩码；
 * 组合项的按下判定是"掩码内所有键同时按下"，出事件时键值就是这个掩码；
 * 组合项按住期间，被它覆盖的成员项本轮不出单击/双击结果（组合优先）。
 * 键掩码类型为 uint16_t：最多支持 16 个键位（bit0 到 bit15）。
 * DevButtonRegister() 登记（键值掩码, 事件）→ 回调并登记要扫的键值，注册即扫描；
 * 同一（键值掩码, 事件）重复注册会被拒绝。 */
void               DevButtonInit(void);
uint8_t            DevButtonRegister(uint16_t u16KeyMask, eDevButtonEventDef eEvent, FuncPtr fun);
void               DevButtonProcessEvents(void);

#ifdef __cplusplus
}
#endif

#endif /* __DEV_BUTTON_H__ */
