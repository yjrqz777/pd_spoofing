#include "user_system.h"

tSysDataDef tSysData;

static void SystemInit(void)
{
    tSysData.eState = E_SYSTEM_POWERON;
    tSysData.u16PowerOnTime = 0u;
    tSysData.u16RunTime = 0u;
}



uint16_t BspSystemTask(void)
{
    PT_BEGIN()
    {
        SystemInit();
    }
    while (1)
    {
        PT_WAIT_UNTIL(SYSTEM_TASK_MS / OS_TICK_MS);

    }
    PT_END();
}
