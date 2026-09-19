#ifndef __BSP_IWDG_H__
#define __BSP_IWDG_H__

#include "user_global.h"

#define IWDG_TASK_MS (1500)

void BspIwdgInit(uint16_t prer, uint16_t rlr);
uint16_t BspIwdgTask(void);
#endif 