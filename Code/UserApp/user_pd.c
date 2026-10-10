/**
 * @file    user_pd.c
 * @brief   PD sink 的产品规则（Application）：要哪一档、什么时候请求、失败怎么办
 *******************************************************************************
 * @note    本模块属于 Application：
 *            - 只决定"要几伏"，报文结构、寄存器、CC 一律不碰；
 *            - 换档与开关的入口给按键回调挂；
 *            - VOUT-EN 走 bsp_gpio.h 的 BspGpioSetVout()。
 *******************************************************************************
 */

#include "user_pd.h"

#include "UserBsp/bsp_gpio.h"

/* ---- 产品状态（按用途分成两组） ---- */

/** @brief 产品目标与输出：要哪一档、输出该不该通 */
typedef struct
{
    uint8_t u8Index;          /* 目标档位在档位表里的下标 */
    /**
     * @brief 1 = 用户要求导通后级输出
     * @note  上电固定为 0：协商照常自动进行（Request 由 Device 层在收到能力报文时
     *        当场发出，与本标志无关），但 VOUT-EN 一直关着，后级不带负载。
     *        只有用户长按 KEY3 触发 UserPdOutputOn() 之后，才会在协商就绪时导通。
     */
    uint8_t u8WantOutput;
    uint8_t u8ReqSwitch;    /* 1 = 还没把目标档位交给 Device */
} tUserPdTgtDef;
;

static tUserPdTgtDef tTgt;    /* 产品目标与输出 */

/** @brief 起 PD 的 PHY（幂等）。按需启动的理由见 USER_PD_AUTOSTART_MS 的注释 */
static void UserPdStart(void)
{
    DevPdInit();
    log_info("PD phy start");
}

/** @brief 把当前目标档位交给 Device；E_OK = 已登记，等它发出去 */
static eStatusDef UserPdRequest(void)
{
    const tDevPdPdoDef *ptPdo = DevPdGetPdo(tTgt.u8Index);

    if (ptPdo == 0)
    {
        return E_ERROR;                             /* 档位表还没解析出来 */
    }

    return DevPdRequestPdo(ptPdo->u8PdoIndex);
}

/** @brief 按协商结果开关 VOUT */
static void UserPdApply(void)
{
    if (tTgt.u8WantOutput != 0u)
    {
        BspGpioSetVout(1u);                     /* 电压已到位，接通输出 */
    }
    else if (tTgt.u8WantOutput == 0u)                     /* 只有用户明确关机才断输出 */
    {
        BspGpioSetVout(0u);
    }
}


static void UserPdService(void)
{
    static uint8_t u8LastIsConnected = 0u;
    static uint8_t u8LastState = E_DEV_PD_IDLE;
    if (DevPdIsConnected() != u8LastIsConnected)        /* 插拔变化打一条，方便看 */
    {
        u8LastIsConnected = DevPdIsConnected();
        if (u8LastIsConnected != 0u)
        {
            tTgt.u8ReqSwitch = 1u;                 /* 刚插上，重新申请目标档位 */
        }
    }

    if (DevPdGetState() != u8LastState)          /* 协商状态变化也打一条 */
    {
        u8LastState = DevPdGetState();
        log_info("PD state %u, pdo %u",
                 (unsigned)u8LastState, (unsigned)DevPdGetPdoCount());
    }

    /* 首次收到能力表后必须完成基础协商，否则源端等待 Request 超时后会
        * Hard Reset 并短暂切断 VBUS。本板由 VBUS 供电，会因此循环复位。
        * 是否导通后级输出仍由 tTgt.u8WantOutput 决定，与 PD 协商解耦。 */
    if ((tTgt.u8ReqSwitch != 0u) && (DevPdIsConnected() != 0u))
    {
        if (UserPdRequest() == E_OK)
        {
            tTgt.u8ReqSwitch = 0u;
        }
    }
}



/* ---- 公共接口 ---- */

uint16_t UserPdTask(void)
{
    PT_BEGIN()
    {
        UserPdStart();
    }
    while (1)
    {
        PT_WAIT_UNTIL(USER_PD_TASK_MS / OS_TICK_MS);
        DevPdService();                             /* 节拍必须与它要求的一致 */
        UserPdService();                            /* 处理目标档位、开关输出 */
        UserPdApply();
    }
    PT_END()
}

void UserPdNextPdo(void)
{
    const tDevPdPdoDef *ptPdo;
    uint8_t u8Count = DevPdGetPdoCount();

    if (u8Count == 0u)
    {
        log_warn("PD no pdo table, state %u", (unsigned)DevPdGetState());
        return;                                     /* 还没收到能力报文，没得选 */
    }

    tTgt.u8Index = (uint8_t)((tTgt.u8Index + 1u) % u8Count);  /* 到顶回到底，调压期间保持输出 */
    tTgt.u8ReqSwitch = 1u;                             /* 只改目标，输出开不开由长按决定 */

    ptPdo = DevPdGetPdo(tTgt.u8Index);
    log_info("PD pdo %u/%u, %u mV %u mA",
             (unsigned)(tTgt.u8Index + 1u), (unsigned)u8Count,
             (unsigned)((ptPdo != 0) ? ptPdo->u16MinVoltageMv : 0u),
             (unsigned)((ptPdo != 0) ? ptPdo->u16MaxCurrentMa : 0u));
}

void UserPdPrevPdo(void)
{
    const tDevPdPdoDef *ptPdo;
    uint8_t u8Count = DevPdGetPdoCount();

    if (u8Count == 0u)
    {
        log_warn("PD no pdo table, state %u", (unsigned)DevPdGetState());
        return;
    }

    if (tTgt.u8Index == 0u)
    {
        tTgt.u8Index = (uint8_t)(u8Count - 1u);          /* 到底回到顶 */
    }
    else
    {
        tTgt.u8Index--;
    }

    tTgt.u8ReqSwitch = 1u;

    ptPdo = DevPdGetPdo(tTgt.u8Index);
    log_info("PD pdo %u/%u, %u mV %u mA",
             (unsigned)(tTgt.u8Index + 1u), (unsigned)u8Count,
             (unsigned)((ptPdo != 0) ? ptPdo->u16MinVoltageMv : 0u),
             (unsigned)((ptPdo != 0) ? ptPdo->u16MaxCurrentMa : 0u));
}

void UserPdOutputOn(void)
{
    tTgt.u8WantOutput = 1u;
}

void UserPdOutputOff(void)
{
    tTgt.u8WantOutput = 0u;
}

eDevPdStateDef UserPdGetState(void)
{
    return DevPdGetState();
}

uint8_t UserPdGetIndex(void)
{
    return tTgt.u8Index;
}

uint8_t UserPdGetPdoCount(void)
{
    return DevPdGetPdoCount();
}

uint8_t UserPdHasPps(void)
{
    return DevPdHasPps();
}

const tDevPdPdoDef *UserPdGetPdo(uint8_t u8PdoIndex)
{
    return DevPdGetPdo(u8PdoIndex);
}

uint16_t UserPdGetTargetMv(void)
{
    const tDevPdPdoDef *ptPdo;
    uint16_t u16Mv = DevPdGetActiveMv();

    if (u16Mv != 0u)
    {
        return u16Mv;                               /* 协商到了就用实际值 */
    }

    ptPdo = DevPdGetPdo(tTgt.u8Index);                   /* 还没协商到就用档位标称值 */
    if (ptPdo != 0)
    {
        u16Mv = ptPdo->u16MinVoltageMv;
    }

    return u16Mv;
}

uint16_t UserPdGetActiveMv(void)
{
    return DevPdGetActiveMv();
}
