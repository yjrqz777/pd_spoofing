/**
 * @file dev_button.h
 * @brief   按键设备层：每条订阅一份状态机，1ms 节拍分频扫描，事件交主循环分发。
 *
 * @note    本模块属于 Device 层：把 GPIO 电平变成按键事件，并分发给注册的回调。
 *          引脚与电平由 BSP 提供，事件对应什么行为由 Application 通过回调决定。
 *          按键表以"按键掩码"为行：一条（键值, 事件）订阅占一行，该行订阅的事件、
 *          回调和它自己的状态机计数都在这行里；行号就是该行的 id。
 *          扫描只推进"有活动"的行：键值有交集，或状态机还没回到空闲。
 *          中断侧逐行采样、消抖、跑状态机，命中本行订阅的事件就把行 id 压入 SPSC 队列；
 *          主循环侧在时间片内用 DevButtonProcessEvents() 取空队列并按 id 调回调。
 
 * @author 段志炫
 * @date 2026-10-15
 * @version 0.1.1
 * @par 修改记录
 * 2026-10-15 按项目注释规范补齐文件和接口说明。*/

#ifndef __DEV_BUTTON_H__
#define __DEV_BUTTON_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "UserBsp/bsp_gpio.h"      /* eBspButtonIdDef：键值掩码 E_BSP_KEY_x */

/* 扫描与判定参数（单位 ms） */
#define DEV_BTN_SCAN_MS       (5u)                          /**< 扫描间隔 */
#define DEV_BTN_QUEUE_SIZE    (16u)                         /**< SPSC 事件队列深度（条目数） */
#define DEV_BTN_DEBOUNCE_NUM  (2u)                          /**< 消抖采样次数 */
#define DEV_BTN_CLICK_WINDOW  (100u / DEV_BTN_SCAN_MS)      /**< 多击判定窗口 */
#define DEV_BTN_LONG_TICKS    (1000u / DEV_BTN_SCAN_MS)     /**< 长按判定阈值 */
#define DEV_BTN_REPEAT_MAX    (2u)                          /**< 连击计数上限 */
#define DEV_BTN_HOLD_UPDATA    (100u)      /**< 长按持续上报节流：100ms 一次 */
#define DEV_BTN_HOLD_DIV      (DEV_BTN_HOLD_UPDATA / DEV_BTN_SCAN_MS)      /**< 长按持续上报节流：100ms 一次 */
#define DEV_BTN_PRESSED       (1u)                          /**< 已消抖的按下状态：1=按下，0=未按下 */
#define DEV_BTN_NUM_MAX      (10) /**< 按键表行数上限：一条（键值, 事件）订阅占一行 */

/* 键值掩码只用 BSP 的 eBspButtonIdDef（E_BSP_KEY_x），本层不另立一套编号 */

/* 按键事件（E_DEV_BTN_NONE 必须为 0） */
typedef enum
{
    E_DEV_BTN_NONE = 0,      /**< 无事件 */
    E_DEV_BTN_DOWN,          /**< 按下 */
    E_DEV_BTN_UP,            /**< 抬起 */
    E_DEV_BTN_REPEAT,        /**< 连击中的再次按下 */
    E_DEV_BTN_SINGLE_CLICK,  /**< 单击 */
    E_DEV_BTN_DOUBLE_CLICK,  /**< 双击 */
    E_DEV_BTN_LONG_PRESS,    /**< 长按开始 */
    E_DEV_BTN_LONG_HOLD,     /**< 长按持续（每 100ms 上报一次） */
    E_DEV_BTN_COUNT          /**< 事件总数（边界标记） */
} eDevButtonEventDef;

/**
 * @brief 初始化按键订阅表和事件队列
 * @param[in] 无 无输入参数。
 * @return 无返回值。
 * @note 调用前确认参数有效，并在模块规定的上下文中调用。
 */
void               DevButtonInit(void);

/**
 * @brief 启动按键周期扫描
 * @param[in] 无 无输入参数。
 * @return 无返回值。
 * @note 必须在所有按键订阅注册完成后调用，且只能调用一次。
 */
void DevButtonStart(void);

/**
 * @brief 注册按键事件及其回调
 * @param[in] keyValue 按键掩码，使用 BSP 定义的按键位掩码。
 * @param[in] event 按键事件类型，取值为按键事件枚举成员。
 * @param[in] fun 按键事件回调函数。
 * @return 函数执行结果或查询值，取值含义见函数说明。
 * @note 必须在 DevButtonInit() 之后、DevButtonStart() 之前调用。
 */
uint8_t            DevButtonRegister(uint16_t keyValue, eDevButtonEventDef event, FuncPtr fun);

/**
 * @brief 取出按键事件并调用对应回调
 * @param[in] 无 无输入参数。
 * @return 无返回值。
 * @note 在主循环上下文周期调用；不要在节拍中断中执行回调。
 */
void               DevButtonProcessEvents(void);

#ifdef __cplusplus
}
#endif

#endif /* __DEV_BUTTON_H__ */
