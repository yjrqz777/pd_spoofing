/**
 * @file    user_button_fun.c
 * @brief   按键功能回调实现
 *******************************************************************************
 * @note    实现按键事件触发的上层功能。
 *          与旧版（FOC 项目）相比，已移除全部电机/PID 调参回调，
 *          改为本项目实际需要的：
 *            - KEY3 双击导通输出、单击关断输出（导通需双击，避免误触）；
 *            - KEY1 / KEY2 单击调整 PD 固定电压档，换档前先断开输出。
 *******************************************************************************
 */

#include "user_button.h"

/**
 * @brief Applies a VOUT-EN level and keeps the status LED in sync.
 * @param[in] u8Enable Non-zero turns the switched output on, zero turns it off.
 * @retval 1 The output actually changed state.
 * @retval 0 The output was already in the requested state.
 */
static uint8_t UsrButtonSetOutput(uint8_t u8Enable)
{
    if ((BspBoardGetVoutEnable() != 0u) == (u8Enable != 0u))
    {
        return 0u;
    }

    /* 状态确实变化才写 GPIO 与指示灯，并打印一次 */
    BspBoardSetVoutEnable(u8Enable);
    BspBoardSetLed(u8Enable);
    printf("VOUT-EN -> %s\r\n", (u8Enable != 0u) ? "ON" : "OFF");

    return 1u;
}

/**
 * @brief Turns the switched VOUT path on (KEY3 double click).
 * @param[in] ptButton Pointer to the button that triggered the action.
 * @note  Double click is required so that a stray touch cannot energise the output.
 */
void UsrButtonOutputOn(Button *ptButton)
{
    (void)ptButton;
    (void)UsrButtonSetOutput(1u);
}

/**
 * @brief Turns the switched VOUT path off (KEY3 single click).
 * @param[in] ptButton Pointer to the button that triggered the action.
 */
void UsrButtonOutputOff(Button *ptButton)
{
    (void)ptButton;
    (void)UsrButtonSetOutput(0u);
}

/**
 * @brief Requests the next lower fixed USB-PD voltage profile.
 * @param[in] ptButton Pointer to the button that triggered the action.
 * @note  The output is switched off first: changing the requested PDO while the
 *        output is live would step the load voltage.
 */
void UsrButtonValueDec(Button *ptButton)
{
    (void)ptButton;
    (void)UsrButtonSetOutput(0u);
    UsrPdSelectPreviousPdo();
}

/**
 * @brief Requests the next higher fixed USB-PD voltage profile.
 * @param[in] ptButton Pointer to the button that triggered the action.
 * @note  Same as UsrButtonValueDec(): the output is switched off before the step.
 */
void UsrButtonValueInc(Button *ptButton)
{
    (void)ptButton;
    (void)UsrButtonSetOutput(0u);
    UsrPdSelectNextPdo();
}
