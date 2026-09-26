#include "user_system.h"

tSysDataDef tSysData;

static void UserSystemInit(void)
{
    tSysData.eState = E_SYSTEM_POWERON;
    tSysData.u16PowerOnTime = 0u;
    tSysData.u16RunTime = 0u;
}

static void SysTimerTick(void)
{
    tSysData.u16PowerOnTime++;

    if (tSysData.u16PowerOnTime > 1000/SYSTEM_TASK_MS && tSysData.eState == E_SYSTEM_POWERON)
    {
        tSysData.eState = E_SYSTEM_OFF;
    }
    


    if (tSysData.eState >= E_SYSTEM_RUN)
    {
        tSysData.u16RunTime++;
    }
}


uint16_t BspSystemTask(void)
{
    PT_BEGIN()
    {
        UserSystemInit();
    }
    while (1)
    {
        PT_WAIT_UNTIL(SYSTEM_TASK_MS / OS_TICK_MS);
        SysTimerTick();

    }
    PT_END();
}
