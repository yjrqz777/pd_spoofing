/**
 * @file    user_button.h
 * @brief   按键应用层：默认订阅条目 + 5ms 事件处理任务。
 *
 * @note    订阅表、注册接口与事件分发在 Device 层（dev_button.{c,h}）：
 *          注册用 DevButtonRegister()，分发用 DevButtonProcessEvents()。
 *          本层只留默认订阅条目和调度任务。
 */

#ifndef __USER_BUTTON_H__
#define __USER_BUTTON_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "UserDev/button/dev_button.h"


/* 注册内置默认条目（原静态表内容），可不调用 */
void     UsrButtonRegisterDefault(void);

uint16_t UsrButtonTask(void);            /* 按键事件处理任务（5ms 时间片） */

#ifdef __cplusplus
}
#endif

#endif /* __USER_BUTTON_H__ */
