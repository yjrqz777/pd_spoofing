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

/**
 * @brief  注册原静态表的默认条目，不调用则订阅表为空。
 */
void UsrButtonRegisterDefault(void)
{
    (void)DevButtonRegister(E_BSP_KEY_1,               E_DEV_BTN_SINGLE_CLICK, UsrButtonValueDec);
    (void)DevButtonRegister(E_BSP_KEY_2,               E_DEV_BTN_SINGLE_CLICK, UsrButtonValueInc);
    (void)DevButtonRegister(E_BSP_KEY_1 | E_BSP_KEY_2, E_DEV_BTN_SINGLE_CLICK, UsrButtonValueDec);
}

uint16_t UsrButtonTask(void)
{
    PT_BEGIN()
    {
        UsrButtonRegisterDefault();
    }

    while (1)
    {
        PT_WAIT_UNTIL(DEV_BTN_SCAN_MS / OS_TICK_MS);   /* 5ms 时间片 */
        DevButtonProcessEvents();
    }

    PT_END();
}
