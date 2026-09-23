/**
 * @file    user_button.h
 * @brief   按键应用层：订阅表把（键组合, 事件）映射为产品行为，支持组合键注册。
 */

#ifndef __USER_BUTTON_H__
#define __USER_BUTTON_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "UserDev/button/dev_button.h"

/* 业务回调：订阅表命中时调用 */
typedef void (*UsrButtonFunDef)(void);

/* 订阅表条目：u16KeyMask 写单个键位值 = 普通键；写多个键按位或 = 组合键。
 * 掩码为 uint16_t，最多支持 16 个键位（bit0 到 bit15）。 */
typedef struct tUsrButtonSubDef
{
    uint16_t           u16KeyMask;  /* E_BSP_KEY_x 按位或 */
    eDevButtonEventDef eEvent;      /* 触发事件 */
    UsrButtonFunDef    fun;         /* 命中后调用 */
} tUsrButtonSubDef;

/* user_button_fun.c 提供的业务回调 */
void UsrButtonOutputOn(void);
void UsrButtonOutputOff(void);
void UsrButtonValueDec(void);
void UsrButtonValueInc(void);

/* 订阅表容量：可注册的（键组合, 事件）条目上限 */
#define USR_BTN_SUB_MAX (8u)

/* 订阅表注册：先 Init 清表，再逐条 Register。匹配是"键值掩码 + 事件"的精确匹配，
 * 注册顺序不影响结果；键值掩码须与 Device 层 DevButtonRegisterKey() 注册的一致。
 * 须在进入主循环、UsrButtonTask() 开始调度之前完成，运行期不再改动。 */
void     UsrButtonInit(void);
uint8_t  UsrButtonRegister(uint16_t u16KeyMask, eDevButtonEventDef eEvent, UsrButtonFunDef fun);
void     UsrButtonRegisterDefault(void);  /* 注册内置默认条目（原静态表内容），可不调用 */

void     UsrButtonProcessEvents(void);   /* 取走并处理全部待处理事件（主循环上下文调用） */
uint16_t UsrButtonTask(void);            /* 按键事件处理任务（5ms 时间片） */

#ifdef __cplusplus
}
#endif

#endif /* __USER_BUTTON_H__ */
