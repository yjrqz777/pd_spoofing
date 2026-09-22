/**
 * @file    user_button.h
 * @brief   按键应用层：订阅表把（键组合, 事件）映射为产品行为，支持组合键注册。
 */

#ifndef __USER_BUTTON_H__
#define __USER_BUTTON_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "user_global.h"

/* 业务回调：订阅表命中时调用 */
typedef void (*UsrButtonFunDef)(void);

/* 订阅表条目：u8KeyMask 写单个键位值 = 普通键；写多个键按位或 = 组合键 */
typedef struct tUsrButtonSubDef
{
    uint8_t            u8KeyMask;   /* E_BSP_KEY_x 按位或 */
    eDevButtonEventDef eEvent;      /* 触发事件 */
    UsrButtonFunDef    fun;         /* 命中后调用 */
} tUsrButtonSubDef;

/* user_button_fun.c 提供的业务回调 */
void UsrButtonOutputOn(void);
void UsrButtonOutputOff(void);
void UsrButtonValueDec(void);
void UsrButtonValueInc(void);

void     UsrButtonProcessEvents(void);   /* 取走并处理全部待处理事件（主循环上下文调用） */
uint16_t UsrButtonTask(void);            /* 按键事件处理任务（5ms 时间片） */

#ifdef __cplusplus
}
#endif

#endif /* __USER_BUTTON_H__ */
