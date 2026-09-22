/**
 * @file    dev_button.h
 * @brief   按键设备层：在 1ms 节拍中断内分频扫描消抖，事件由主循环取用。
 *
 * @note    本模块属于 Device 层：只负责把 GPIO 电平变成按键事件。
 *          引脚与电平由 BSP 提供，事件对应什么行为由 Application 决定。
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

/* 只暴露接口；按键上下文结构体保留在本模块的 .c 内。
 * 键值为位值（E_BSP_KEY_x = 1/2/4），可按位或组成组合键掩码；
 * GetEvent/IsPressed 只接受单键位值，PendingMask/ClearEvent 接受任意键组合掩码。
 * 键掩码类型为 uint16_t：最多支持 16 个键位（bit0 到 bit15）。 */
void               DevButtonInit(void);
eDevButtonEventDef DevButtonGetEvent(eBspButtonIdDef eKey);
uint8_t            DevButtonIsPressed(eBspButtonIdDef eKey);
uint16_t           DevButtonPendingMask(eDevButtonEventDef eEvent);
void               DevButtonClearEvent(uint16_t u16KeyMask, eDevButtonEventDef eEvent);

#ifdef __cplusplus
}
#endif

#endif /* __DEV_BUTTON_H__ */
