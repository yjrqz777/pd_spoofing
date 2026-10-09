/**
 * @file    user_pd.h
 * @brief   PD sink 的产品规则（Application）：要哪一档、什么时候请求、失败怎么办
 *******************************************************************************
 * @note    本模块属于 Application：
 *            - 只决定"要几伏"，不碰报文结构、不碰寄存器、不碰 CC；
 *            - 换档与开关的入口给按键回调挂；
 *            - 协商状态和当前电压给界面、日志用。
 *******************************************************************************
 */

#ifndef __USER_PD_H__
#define __USER_PD_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "user_global.h"
#include "UserDev/pd/dev_pd.h"

/** @brief PD 任务节拍（毫秒）：必须与 Device 层要求的调用周期一致 */
#define USER_PD_TASK_MS  DEV_PD_SERVICE_MS

/** @brief 上电后默认想请求第几个档位（档位表下标，不报文里的序号） */
#define USER_PD_DEFAULT_INDEX  (0u)

/**
 * @brief 上电后延时多久自动启动 PD 的 PHY（毫秒）
 * @note  不能开机就起：PD 的模拟前端（CC 比较器、BMC 收发器）一上电就从 3V3
 *        抽电流，余量小的板子会被它拉塌成 POR 复位。这里给 3V3 和 LCD 一点时间
 *        进入稳态再起。设成 0 表示不自动起，只能靠按键（KEY3 长按）。
 */
#define USER_PD_AUTOSTART_MS  (1000u)

/** @brief 任务函数，主循环里 PT_TASK_REG 注册 */
uint16_t UserPdTask(void);

/* ---- 按键入口 ---- */

/** @brief 往上换一档（到顶回到底） */
void UserPdNextPdo(void);

/** @brief 往下换一档（到底回到顶） */
void UserPdPrevPdo(void);

/** @brief 按当前目标档位开始协商并导通输出 */
void UserPdOutputOn(void);

/** @brief 停止请求并断开输出 */
void UserPdOutputOff(void);

/* ---- 给界面与日志 ---- */

/** @brief 协商状态 */
eDevPdStateDef UserPdGetState(void);

/** @brief 当前目标档位在档位表里的下标 */
uint8_t UserPdGetIndex(void);

/** @brief 可用档位个数 */
uint8_t UserPdGetPdoCount(void);

/** @brief 对端支不支持可调档（PPS）：1 = 支持 */
uint8_t UserPdHasPps(void);

/**
 * @brief  取一个可用档位
 * @param[in] u8Index 档位表下标
 * @return 档位指针；越界返回 0
 */
const tDevPdPdoDef *UserPdGetPdo(uint8_t u8Index);

/** @brief 目标电压（mV）：已协商用实际值，未协商用档位标称值 */
uint16_t UserPdGetTargetMv(void);

/** @brief 实际到位的电压（mV）；没协商到返回 0 */
uint16_t UserPdGetActiveMv(void);

/** @brief 1 = 协商完成、输出已导通 */
uint8_t UserPdIsReady(void);

#ifdef __cplusplus
}
#endif

#endif /* __USER_PD_H__ */
