#ifndef __BSP_IWDG_H__
#define __BSP_IWDG_H__

#include "user_global.h"

#define IWDG_TASK_MS (1500)

void BspIwdgInit(u16 prer, u16 rlr);
uint16_t BspIwdgTask(void);
#endif 