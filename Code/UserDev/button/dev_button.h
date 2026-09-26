/**
 * @file    dev_button.h
 * @brief   按键设备层：每条订阅一份状态机，1ms 节拍分频扫描，事件交主循环分发。
 *
 * @note    本模块属于 Device 层：把 GPIO 电平变成按键事件，并分发给注册的回调。
 *          引脚与电平由 BSP 提供，事件对应什么行为由 Application 通过回调决定。
 *          按键表以"按键掩码"为行：一条（键值, 事件）订阅占一行，该行订阅的事件、
 *          回调和它自己的状态机计数都在这行里；行号就是该行的 id。
 *          扫描只推进"有活动"的行：键值有交集，或状态机还没回到空闲。
 *          中断侧逐行采样、消抖、跑状态机，命中本行订阅的事件就把行 id 压入 SPSC 队列；
 *          主循环侧在时间片内用 DevButtonProcessEvents() 取空队列并按 id 调回调。
 */

#ifndef __DEV_BUTTON_H__
#define __DEV_BUTTON_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "UserBsp/bsp_gpio.h"      /* eBspButtonIdDef：键值掩码 E_BSP_KEY_x */

/* 扫描与判定参数（单位 ms） */
#define DEV_BTN_SCAN_MS       (5u)                          /* 扫描间隔 */
#define DEV_BTN_QUEUE_SIZE    (16u)                         /* SPSC 事件队列深度（条目数） */
#define DEV_BTN_DEBOUNCE_NUM  (2u)                          /* 消抖采样次数 */
#define DEV_BTN_CLICK_WINDOW  (100u / DEV_BTN_SCAN_MS)      /* 多击判定窗口 */
#define DEV_BTN_LONG_TICKS    (1000u / DEV_BTN_SCAN_MS)     /* 长按判定阈值 */
#define DEV_BTN_REPEAT_MAX    (2u)                          /* 连击计数上限 */
#define DEV_BTN_HOLD_DIV      (100u / DEV_BTN_SCAN_MS)      /* 长按持续上报节流：100ms 一次 */
#define DEV_BTN_PRESSED       (1u)                          /* 已消抖的按下状态：1=按下，0=未按下 */
#define DEV_BTN_NUM_MAX      (10) /* 按键表行数上限：一条（键值, 事件）订阅占一行 */

/* 键值掩码只用 BSP 的 eBspButtonIdDef（E_BSP_KEY_x），本层不另立一套编号 */

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

/**
 * @brief  初始化：清空订阅表与事件队列。
 * @note   模块的调用顺序固定为 Init → Register → Start，全程只在主循环上下文。
 *         Start 之后再调 Init 或 Register 会触发断言（那时中断已经在扫表）。
 */
void               DevButtonInit(void);

/**
 * @brief  开始扫描：把按键扫描挂到 1ms 节拍回调上。
 * @note   必须在所有注册完成之后调用，且只能调用一次；调用后表不再允许改动。
 */
void DevButtonStart(void);

/**
 * @brief  注册一条订阅：某按键（单键或组合）产生某事件时调用 fun。
 * @note   须在 DevButtonInit() 之后、DevButtonStart() 之前调用。
 */
uint8_t            DevButtonRegister(uint16_t keyValue, eDevButtonEventDef event, FuncPtr fun);

/**
 * @brief  取空事件队列并按行 id 分发回调。
 * @note   主循环上下文周期调用（UsrButtonTask 的 5ms 时间片）。
 */
void               DevButtonProcessEvents(void);

#ifdef __cplusplus
}
#endif

#endif /* __DEV_BUTTON_H__ */
