/**
 * @file    user_button.c
 * @brief   按键应用层：默认订阅条目 + 5ms 事件处理任务。
 *
 * @note    订阅表、注册与分发的实现都在 Device 层（dev_button.c）；本层只声明
 *          "默认把哪些（键值掩码, 事件）绑到哪个回调"，并在时间片里驱动
 *          DevButtonProcessEvents() 取空队列。
 *
 *          DevButtonRegister() 登记订阅时会把键值一并记进扫描表，不用另外注册键值。
 */

#include "user_button.h"
#include "Task.h"                    /* PT_BEGIN/PT_WAIT_UNTIL/OS_TICK_MS */

extern void UsrButtonPdInc(void);
extern void UsrButtonPdDec(void);
extern void UsrButtonPdON(void);
extern void UsrButtonPdTest(void);
/**
 * @brief  注册原静态表的默认条目，不调用则订阅表为空。
 */
void UsrButtonRegisterDefault(void)
{
    (void)DevButtonRegister(E_BSP_KEY_1,               E_DEV_BTN_SINGLE_CLICK, UsrButtonPdInc);
    (void)DevButtonRegister(E_BSP_KEY_2,               E_DEV_BTN_SINGLE_CLICK, UsrButtonPdDec);
    (void)DevButtonRegister(E_BSP_KEY_3,               E_DEV_BTN_SINGLE_CLICK, UsrButtonPdON);
    (void)DevButtonRegister(E_BSP_KEY_1 | E_BSP_KEY_2, E_DEV_BTN_SINGLE_CLICK, UsrButtonPdTest);
}

uint16_t UsrButtonTask(void)
{
    PT_BEGIN()
    {
        DevButtonInit();            
        UsrButtonRegisterDefault();
        DevButtonStart();   
    }

    while (1)
    {
        PT_WAIT_UNTIL(DEV_BTN_SCAN_MS / OS_TICK_MS);   /* 5ms 时间片 */
        DevButtonProcessEvents();
    }

    PT_END();
}
