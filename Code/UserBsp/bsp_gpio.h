#ifndef __BSP_GPIO_H__
#define __BSP_GPIO_H__

#include "user_global.h"

typedef enum eBspButtonIdDef
{
    E_BSP_KEY_1 = 0,     /**< KEY1：PB3，低电平有效 */
    E_BSP_KEY_2,         /**< KEY2：PB4，低电平有效 */
    E_BSP_KEY_3,         /**< KEY3：PB6，低电平有效 */
    E_BSP_KEY_NUM        /**< 按键数量（边界标记） */
} eBspButtonIdDef;

void BspGpioSetLed(uint8_t val);
void BspGpioSetVout(uint8_t val);
void BspLedToggle(void);
void BspGpioInit(void);
uint8_t BspGpioGetButtonLevel(eBspButtonIdDef button_id);
#endif 