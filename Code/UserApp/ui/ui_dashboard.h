/**
 * @file    ui_dashboard.h
 * @brief   240x135 横屏仪表界面头文件（静态布局 + 局部刷新）
 *******************************************************************************
 * @note    界面划分（见 doc/LCD_DISPLAY_IMPLEMENTATION_PLAN.md §4）：
 *            - 满屏铺满：各区域直接贴到屏幕四边，不留外围边距；
 *            - 顶部状态栏：左侧 "POWER MONITOR"，右侧 ONLINE / 空白（均为 16px）；
 *            - VBUS / VOUT 卡片：24px 大号数值 + 8px 单位；
 *            - POWER / IBUS / OUTPUT 三个小区域：16px 数值 + 开关槽。
 *
 *          数据流：
 *            UiDashboard_Init()      清屏并只画一次背景、卡片、边框与标签；
 *            UiDashboard_SetData()   拷贝一帧数据，不碰 SPI；
 *            UiDashboard_Refresh()   与上一帧比较，只重绘变化的矩形。
 *
 *          本模块不做浮点格式化、不用动态内存、不分配整屏帧缓冲，
 *          所有坐标都被约束在 240x135 内。
 *******************************************************************************
 */

#ifndef __UI_DASHBOARD_H__
#define __UI_DASHBOARD_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdbool.h>

/** @brief 页面数据（界面层只接受工程单位整数，不使用浮点） */
typedef struct
{
    uint32_t vbus_mv;          /**< 输入母线电压（mV） */
    uint32_t vout_mv;          /**< 输出电压（mV） */
    uint32_t ibus_ma;          /**< 输入电流（mA） */
    uint32_t power_mw;         /**< 输入功率（mW），= VBUS x IBUS */
    bool     output_enabled;   /**< VOUT-EN 命令状态 */
    bool     measurements_valid;/**< 采样结果是否有效 */
} UiPowerData;

/**
 * @brief  初始化页面：整屏清成页面背景色，并绘制全部静态内容
 * @note   整屏填充是阻塞的（约 45ms @12MHz SPI），只在进入本页面时调用一次；
 *         之后 UiDashboard_Refresh() 只做小矩形重绘。
 */
void UiDashboard_Init(void);

/**
 * @brief  提交一帧数据
 * @param[in] ptData  数据，内部只做拷贝，本函数不操作 SPI
 */
void UiDashboard_SetData(const UiPowerData *ptData);

/**
 * @brief  按需刷新动态区域
 * @note   与上一帧比较：格式化后的字符串（或开关状态）没变的矩形不重绘。
 *         每 200ms（5Hz）调用一次即可。
 */
void UiDashboard_Refresh(void);

#ifdef __cplusplus
}
#endif

#endif /* __UI_DASHBOARD_H__ */
