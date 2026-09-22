#include "bsp_iwdg.h"
#include "bsp_gpio.h"      /* BspLedToggle：喂狗任务里翻转指示灯 */



/*********************************************************************
 * @fn      IWDG_Init
 *
 * @brief   Initializes IWDG.
 *
 * @param   IWDG_Prescaler: specifies the IWDG Prescaler value.
 *            IWDG_Prescaler_4: IWDG prescaler set to 4.
 *            IWDG_Prescaler_8: IWDG prescaler set to 8.
 *            IWDG_Prescaler_16: IWDG prescaler set to 16.
 *            IWDG_Prescaler_32: IWDG prescaler set to 32.
 *            IWDG_Prescaler_64: IWDG prescaler set to 64.
 *            IWDG_Prescaler_128: IWDG prescaler set to 128.
 *            IWDG_Prescaler_256: IWDG prescaler set to 256.
 *          Reload: specifies the IWDG Reload value.
 *            This parameter must be a number between 0 and 0x0FFF.
 *          WdgTImeOut = prer/47000 * rlr (ms)
 * @return  none
 */

void BspIwdgInit(uint16_t prer, uint16_t rlr)
{
	IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
	IWDG_SetPrescaler(prer);
	IWDG_SetReload(rlr);
	IWDG_ReloadCounter();
	IWDG_Enable();
}


uint16_t BspIwdgTask(void)
{
    PT_BEGIN()
    {
    }
    while (1)
    {
        PT_WAIT_UNTIL(IWDG_TASK_MS / OS_TICK_MS);
        IWDG_ReloadCounter();	//Feed dog
        BspLedToggle();
    }
    PT_END();
}


