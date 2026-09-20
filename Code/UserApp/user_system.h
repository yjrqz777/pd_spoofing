#ifndef __USER_SYSTEM_H__
#define __USER_SYSTEM_H__

#include "user_global.h"


typedef enum eSystemStateDef
{
    E_SYSTEM_POWERON = 0u,
    E_SYSTEM_OFF,
    E_SYSTEM_RUN,
    E_SYSTEM_MAX
} eSystemStateDef;

typedef struct tSysDataDef
{
    eSystemStateDef eState;
    uint16_t u16PowerOnTime;
    uint16_t u16RunTime;
} tSysDataDef;

tSysDataDef tSysData;
#define SYSTEM_TASK_MS (10)

uint16_t BspSystemTask(void);

#endif 