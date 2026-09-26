#ifndef __BSP_GPIO_H__
#define __BSP_GPIO_H__

#include "user_global.h"

typedef enum eBspButtonIdDef
{
    E_BSP_KEY_1 = 0,  /**< KEY1：PB3，低电平有效*/
    E_BSP_KEY_2 = 1,  /**< KEY2：PB4，低电平有效 */
    E_BSP_KEY_3 = 2,  /**< KEY3：PB6，低电平有效 */
    E_BSP_KEY_MAX = 3, /**< 所有按键 */
} eBspButtonIdDef;


// #define BSP_BUTTON_TABLE(X) \
//     X(E_BSP_KEY_1, 1)       \
//     X(E_BSP_KEY_2, 2)       \
//     X(E_BSP_KEY_3, 3)

// typedef enum eBspButtonIdDef
// {
// #define X(id, val) id = val,
//     BSP_BUTTON_TABLE(X)
// #undef X
// } eBspButtonIdDef;

// enum
// {
// #define X(id, val) +1
//     E_BSP_KEY_COUNT = 0 BSP_BUTTON_TABLE(X)
// #undef X
// };

void BspGpioSetLed(uint8_t val);
void BspGpioSetVout(uint8_t val);
void BspLedToggle(void);
void BspGpioInit(void);
uint16_t BspGpioGetButtonLevel(eBspButtonIdDef ebuttonId);
#endif 