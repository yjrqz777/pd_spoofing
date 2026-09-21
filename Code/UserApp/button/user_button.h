/**
 * @file    user_button.h
 * @brief   用户按键管理头文件 — 3 按键事件管理
 *******************************************************************************
 * @note    封装 multi-button 库，支持单击/双击/长按/重复等事件
 *******************************************************************************
 */

#ifndef __USER_BUTTON_H__
#define __USER_BUTTON_H__

#include "User_global.h"

/** @brief 按键扫描调度周期（毫秒） */
#define USR_BUTTON_TASK_INTERVAL_MS (5u)

/** @brief 冻结实现使用的兼容调度周期（毫秒） */
#define BUTTON_TIME_MS (5u)

#define USER_BUTTON_COUNT        (3u) /* Board button count. */
#define USER_BUTTON_ACTIVE_LEVEL (0u) /* Buttons are active low. */

void UsrButtonInit(void);
uint8_t UsrButtonGetRawMask(void);
uint8_t UsrButtonGetPressed(uint8_t u8ButtonId);
uint8_t UsrButtonGetPressedMask(void);
uint8_t UsrButtonGetLastEvent(uint8_t u8ButtonId);
uint16_t UsrButtonTask(void);

#endif /* __USER_BUTTON_H__ */
