#ifndef __BSP_GPIO_H__
#define __BSP_GPIO_H__

#include "user_global.h"

typedef enum eBspButtonIdDef
{
    E_BSP_KEY_1 = 0x01,  /**< KEY1：PB3，低电平有效（位值，可按位或组成组合键） */
    E_BSP_KEY_2 = 0x02,  /**< KEY2：PB4，低电平有效 */
    E_BSP_KEY_3 = 0x04,  /**< KEY3：PB6，低电平有效 */
    E_BSP_KEY_NUM = 3    /**< 按键数量（边界标记，不是键值） */
} eBspButtonIdDef;

void BspGpioSetLed(uint8_t val);
void BspGpioSetVout(uint8_t val);
void BspLedToggle(void);
void BspGpioInit(void);
uint8_t BspGpioGetButtonLevel(eBspButtonIdDef button_id);
#endif 