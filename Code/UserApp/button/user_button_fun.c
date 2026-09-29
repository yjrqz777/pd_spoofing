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

static uint8_t i = 0;
void UsrButtonPdInc(void)
{
    log_info("UsrButtonPdInc");
    BspGpioSetVout(0);
}

void UsrButtonPdDec(void)
{
    log_info("UsrButtonPdDec");
    BspGpioSetVout(0);
}

void UsrButtonPdON(void)
{
    log_info("UsrButtonPdON");
    BspGpioSetVout(0);
    if (tSysData.eState < E_SYSTEM_OFF)
    {
        return;
    }
    if (tSysData.eState == E_SYSTEM_OFF)
    {
        tSysData.eState = E_SYSTEM_RUN;
        return;
    }
    tSysData.eState = E_SYSTEM_OFF;
    

}

void UsrButtonPdTest(void)
{
    log_info("UsrButtonPdTest");
}

void UserButtonPowerOn(void)
{
    if (tSysData.eState < E_SYSTEM_RUN)
    {
        return;
    }
    log_info("UsrButton Power ON %d", i);
    if (i++ > 3000 / DEV_BTN_HOLD_UPDATA)
    {
        BspGpioSetVout(1);
        i = 0;
    }
}

void UserButtonPowerOnUp(void)
{
    i = 0;
    log_info("UsrButton Power Up %d", i);
}

