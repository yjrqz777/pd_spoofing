#ifndef __BSP_GPIO_H__
#define __BSP_GPIO_H__

#include "user_global.h"

/**
 * @brief 按键键值定义：一个 bit 就是一个物理按键，组合按键按位或。
 * @note  这里是掩码值（不是索引），可直接作为 DevButtonRegister() 的键值参数；
 *        组合按键写 E_BSP_KEY_1 | E_BSP_KEY_2 这样按位或的形式。
 */
typedef enum eBspButtonIdDef
{
    E_BSP_KEY_1   = 0x01,  /**< KEY1：PB3，低电平有效 */
    E_BSP_KEY_2   = 0x02,  /**< KEY2：PB4，低电平有效 */
    E_BSP_KEY_3   = 0x04,  /**< KEY3：PB6，低电平有效 */
    E_BSP_KEY_ALL = 0x07,  /**< 全部按键的掩码 */
    E_BSP_KEY_NUM = 3,     /**< 按键数量（可用掩码空间 = 1 << NUM） */
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
uint16_t BspGpioGetButtonMask(void);
#endif 