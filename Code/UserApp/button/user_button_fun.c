/**
 * @file    user_button_fun.c
 * @brief   按键功能回调实现
 *******************************************************************************
 * @note    实现按键事件触发的上层功能。
 *          与旧版（FOC 项目）相比，已移除全部电机/PID 调参回调，
 *          改为本项目实际需要的：
 *            - KEY3 双击导通输出、单击关断输出（导通需双击，避免误触）；
 *            - KEY1 / KEY2 单击调整 PD 固定电压档，换档前先断开输出。
 *          回调签名统一为 void(void)，由 dev_button.c 的订阅表调用。
 *******************************************************************************
 */

#include <stdio.h>
#include "user_button.h"
#include "UserApp/user_system.h"
#include "Components/log/src/log.h"
void UsrButtonPdInc(void)
{
    log_info("UsrButtonPdInc");
}

void UsrButtonPdDec(void)
{
    log_info("UsrButtonPdDec");
}

void UsrButtonPdON(void)
{
    log_info("UsrButtonPdON");
    tSysData.eState = E_SYSTEM_RUN;
}

void UsrButtonPdTest(void)
{
    log_info("UsrButtonPdTest");
}
