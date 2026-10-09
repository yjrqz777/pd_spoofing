/**
 * @file    dev_pd.c
 * @brief   USB-PD sink 协议层（Device）：报文编解码、能力报文解析、协商状态机
 *******************************************************************************
 * @note    移植自 tools/EVT/EXAM/USBPD/USBPD_SNK（WCH 官方 sink 例程）的
 *          PD_Main_Proc / PD_Load_Header / PD_PDO_Analyse / PDO_Request，按分层重写：
 *            - 收发只走 bsp_pdsink.h，USBPD 寄存器一个都不碰；
 *            - 例程的 PD_Ctl 收成下面这组 static 变量，上层拿不到句柄；
 *            - 例程的 Tmr_Ms_Dlt 改成调用次数计数，单位是 DEV_PD_SERVICE_MS；
 *            - 例程的 printf 全部去掉，状态通过 DevPdGetState() 给上层；
 *            - 例程的 PD_Detect 挪到 BSP 的 BspPdDetectCc()，本层只做防抖和判定。
 *******************************************************************************
 */

#include "dev_pd.h"
#include "UserBsp/bsp_pdsink.h"

/* ---- 模块内部上下文（按用途分成四组） ---- */

/** @brief 能力报文与档位表：当前一份 + 上一份，用来判断内容有没有变
 *  @note  可调档（PPS）不进 atPdo，只把"有没有"记在 u8HasPps 里。 */
typedef struct
{
    tDevPdPdoDef atPdo[DEV_PD_PDO_MAX];      /* 解析出来的固定档表 */
    tDevPdPdoDef atPdoOld[DEV_PD_PDO_MAX];   /* 上一份，和这一份比对 */
    uint32_t     au32Raw[DEV_PD_PDO_MAX];    /* 能力报文原始 32 位字（诊断用） */
    uint8_t      u8Count;                    /* 表里有几档 */
    uint8_t      u8CountOld;                 /* 上一份有几档 */
    uint8_t      u8HasPps;                   /* 1 = 报文里有可调档 */
    uint8_t      u8HasPpsOld;                /* 上一份有没有可调档 */
    uint8_t      u8RawCount;                 /* 原始字存了几个 */
} tDevPdCapDef;

/** @brief 协商状态：走到哪一步、要哪一档、超时与失败计数 */
typedef struct
{
    eDevPdStateDef eState;        /* 协商状态 */
    uint8_t        u8ActiveIndex; /* 生效档位在报文里的序号，0xFF = 还没有 */
    uint16_t       u16ActiveMv;   /* 生效电压（mV） */
    uint8_t        u8PendIndex;   /* 上层挑好的档位序号，0 = 还没挑 */
    uint8_t        u8ReqIndex;    /* 正在请求的档位序号 */
    uint8_t        u8MsgId;       /* 报文 ID，3 位，0 到 7 */
    uint16_t       u16Timer;      /* 超时计数，单位 DEV_PD_SERVICE_MS */
    uint8_t        u8ErrCnt;      /* 本轮失败次数 */
} tDevPdLinkDef;

/** @brief 连接检测：CC 上有没有源端、检测分频与防抖计数 */
typedef struct
{
    uint8_t u8Connected;          /* 1 = CC 上确认有连接 */
    uint8_t u8DetDiv;             /* 检测分频计数 */
    uint8_t u8DetCnt;             /* 连续确认计数 */
} tDevPdDetDef;

/** @brief 收发缓冲 */
typedef struct
{
    uint8_t au8Rx[BSP_PD_FRAME_MAX];   /* 收帧缓冲，从 BSP 取过来 */
    uint8_t au8Tx[6];                  /* 发帧缓冲：2 字节头 + 最多 1 个数据对象 */
} tDevPdBufDef;

static tDevPdCapDef  tCap;    /* 能力报文与档位表 */
static tDevPdLinkDef tLink;   /* 协商状态 */
static tDevPdDetDef  tDet;    /* 连接检测 */
static tDevPdBufDef  tBuf;    /* 收发缓冲 */

/* ---- 内部：组报文 ---- */

/**
 * @brief  组报文头，放在 tBuf.au8Tx[0..1]
 * @param[in] u8Ext     1 = 扩展报文
 * @param[in] u8MsgType 消息类型，占 bit[4:0]
 * @note   协议版本写 01 = PD2.0；电源角色和数据角色都写 0（SINK / UFP）；
 *         消息 ID 占 bit[3:1]，只有 3 位，每成功发一条加 1 回绕。
 */
static void PdLoadHeader(uint8_t u8Ext, uint8_t u8MsgType)
{
    tBuf.au8Tx[0] = (uint8_t)(u8MsgType & 0x1Fu);
    tBuf.au8Tx[0] |= 0x40u;                              /* bit[7:6] = 01，PD2.0 */

    tBuf.au8Tx[1] = (uint8_t)(tLink.u8MsgId << 1);             /* bit[3:1] 消息 ID */
    if (u8Ext != 0u)
    {
        tBuf.au8Tx[1] |= 0x80u;                          /* bit15 扩展报文 */
    }
}

/**
 * @brief  把组好的报文发出去，等对端 GoodCRC
 * @param[in] u8DataLen 数据对象字节数，0 或 4
 * @retval E_OK    对端已确认
 * @retval E_ERROR 重发完仍没确认
 * @note   GoodCRC 的等待和重发在 BSP 的 BspPdSend() 里，本函数只管组报文和记 ID。
 *         重发时消息 ID 必须不变，对端才认得出是重发，所以 ID 只在成功后加。
 */
static eStatusDef PdSend(uint8_t u8DataLen)
{
    tBuf.au8Tx[1] |= (uint8_t)((u8DataLen / 4u) << 4);   /* bit[6:4] 数据对象个数 */

    if (BspPdSend(tBuf.au8Tx, (uint8_t)(2u + u8DataLen)) == E_OK)
    {
        tLink.u8MsgId = (uint8_t)((tLink.u8MsgId + 1u) & 0x07u);
        return E_OK;
    }

    log_warn("PD tx failed, type %u", (unsigned)(tBuf.au8Tx[0] & 0x1Fu));   /* 对端没回 GoodCRC */
    return E_ERROR;
}

/* ---- 内部：解报文 ---- */

/**
 * @brief  取出一个 PDO 的原始 32 位字
 * @param[in] pu8Cap  PDO 数组首地址
 * @param[in] u8Index 在能力报文里的序号，从 1 开始
 * @return 组装好的 32 位字（线上是小端）
 * @note   型号判定看 bit[31:30]（00 固定、01 电池、10 可变、11 可调）；
 *         可调档里 bit[29:28] 是子类型，PPS 为 00。
 *         诊断日志也用它，保证打印的和解析的是同一个值。
 */
static uint32_t PdPdoRaw(const uint8_t *pu8Cap, uint8_t u8Index)
{
    const uint8_t *pu8 = &pu8Cap[(uint16_t)(u8Index - 1u) * 4u];
    uint32_t u32Raw;

    u32Raw  = (uint32_t)pu8[0];
    u32Raw |= ((uint32_t)pu8[1] << 8);
    u32Raw |= ((uint32_t)pu8[2] << 16);
    u32Raw |= ((uint32_t)pu8[3] << 24);

    return u32Raw;
}

/**
 * @brief  解析一个 PDO，算出电压和电流
 * @param[in]  u8Index 在能力报文里的序号，从 1 开始
 * @param[in]  pu8Cap  PDO 数组首地址
 * @param[out] ptPdo   解析结果
 * @note   PD 的数据对象在线上是小端，先拼成一个 32 位数再按位段取。
 */
static void PdPdoParse(uint8_t u8Index, const uint8_t *pu8Cap, tDevPdPdoDef *ptPdo)
{
    uint32_t u32Raw = PdPdoRaw(pu8Cap, u8Index);

    ptPdo->u8PdoIndex = u8Index;
    ptPdo->eType = (eDevPdPdoTypeDef)((u32Raw >> 30) & 0x03u);   /* bit[31:30] 电源类型 */

    if (ptPdo->eType == E_DEV_PD_PDO_APDO)
    {
        /* 可调档 PPS：bit[24:17] 最大电压、bit[16:8] 最小电压（100mV）、bit[7:0] 最大电流（50mA） */
        ptPdo->u16MaxVoltageMv = (uint16_t)(((u32Raw >> 17) & 0xFFu) * 100u);
        ptPdo->u16MinVoltageMv = (uint16_t)(((u32Raw >> 8) & 0x1FFu) * 100u);
        ptPdo->u16MaxCurrentMa = (uint16_t)((u32Raw & 0xFFu) * 50u);
    }
    else
    {
        /* 固定档：bit[19:10] 电压（50mV 一档）、bit[9:0] 最大电流（10mA 一档） */
        ptPdo->u16MinVoltageMv = (uint16_t)(((u32Raw >> 10) & 0x3FFu) * 50u);
        ptPdo->u16MaxVoltageMv = ptPdo->u16MinVoltageMv;
        ptPdo->u16MaxCurrentMa = (uint16_t)((u32Raw & 0x3FFu) * 10u);
    }
}

/**
 * @brief  保存并解析能力报文
 * @param[in] u8Len 收到的帧总长，含尾部 4 字节 CRC
 * @retval 1 档位表跟上次不一样；0 没变化或者帧无效
 * @note   可调档（PPS）不放进换档表 —— 请求它的报文格式和固定档不一样，
 *         这里只把"有没有 PPS"记下来给上层看。换档表里全是固定档。
 *         源端会周期性重发能力报文，内容没变时靠返回值让上层别刷日志。
 */
static uint8_t PdSaveSrcCap(uint8_t u8Len)
{
    tDevPdPdoDef tPdo;
    uint8_t u8Ndo;
    uint8_t u8Changed;
    uint8_t i;

    u8Ndo = (uint8_t)((tBuf.au8Rx[1] >> 4) & 0x07u);     /* 报文头 bit[14:12] 数据对象个数 */
    if (u8Ndo == 0u)
    {
        tCap.u8Count = 0u;
        tCap.u8HasPps   = 0u;
        return 0u;
    }
    if (u8Ndo > (uint8_t)DEV_PD_PDO_MAX)
    {
        u8Ndo = (uint8_t)DEV_PD_PDO_MAX;
    }
    if ((uint8_t)(2u + (u8Ndo * 4u)) > (uint8_t)(u8Len - 4u))
    {
        return 0u;                                  /* 帧长不够，丢弃 */
    }

    tCap.u8Count = 0u;
    tCap.u8HasPps   = 0u;
    tCap.u8RawCount = u8Ndo;                          /* 诊断：原始字一并存下来 */

    for (i = 0u; i < u8Ndo; i++)
    {
        tCap.au32Raw[i] = PdPdoRaw(&tBuf.au8Rx[2], (uint8_t)(i + 1u));
        PdPdoParse((uint8_t)(i + 1u), &tBuf.au8Rx[2], &tPdo);

        if (tPdo.eType == E_DEV_PD_PDO_APDO)
        {
            tCap.u8HasPps = 1u;                          /* 支持可调档 */
            continue;
        }

        if (tCap.u8Count < (uint8_t)DEV_PD_PDO_MAX)
        {
            tCap.atPdo[tCap.u8Count] = tPdo;
            tCap.u8Count++;
        }
    }

    u8Changed = 0u;
    if ((tCap.u8Count != tCap.u8CountOld) || (tCap.u8HasPps != tCap.u8HasPpsOld))
    {
        u8Changed = 1u;
    }
    else if (memcmp(tCap.atPdo, tCap.atPdoOld, (uint16_t)tCap.u8Count * sizeof(tDevPdPdoDef)) != 0)
    {
        u8Changed = 1u;
    }

    if (u8Changed != 0u)                            /* 变了才更新基准 */
    {
        memcpy(tCap.atPdoOld, tCap.atPdo, sizeof(tCap.atPdo));
        tCap.u8CountOld = tCap.u8Count;
        tCap.u8HasPpsOld   = tCap.u8HasPps;
    }

    return u8Changed;
}

static void PdTryRequest(void);

/** @brief 处理一条收到的报文 */
static void PdHandleMsg(uint8_t u8Len)
{
    uint8_t u8Type = (uint8_t)(tBuf.au8Rx[0] & 0x1Fu);   /* bit[4:0] 消息类型 */
    uint8_t u8Changed;
    uint8_t i;

    switch (u8Type)
    {
        case DEF_TYPE_SRC_CAP:
            tLink.u8ErrCnt = 0u;
            u8Changed = PdSaveSrcCap(u8Len);

            if (tCap.u8Count != 0u)
            {
                tLink.eState = E_DEV_PD_WAIT_SRC_CAP;     /* 档位表有了，紧接着在这里发 Request */
                tLink.u16Timer = 0u;

                /* 关键：源端要求受电端在 tSenderResponse（24 到 30ms）内把 Request
                   发出去，超时它就判定协议出错并撤掉 VBUS（本板 3V3 从 VBUS 变来，
                   一撤就整板掉电）。所以这里第一件事就是发 Request：
                     - 上层还没挑档位时，默认要第一档（5V）；
                     - 任何打印都放在发送之后，日志一行就要几毫秒，放前面会吃掉整个窗口。 */
                if (tLink.u8PendIndex == 0u)
                {
                    tLink.u8PendIndex = tCap.atPdo[0].u8PdoIndex;
                }
                PdTryRequest();

                if (u8Changed == 0u)
                {
                    break;                          /* 源端周期性重发，内容没变，不刷日志 */
                }

                log_info("PD support: yes, %u pdo, pps %u",
                         (unsigned)tCap.u8Count, (unsigned)tCap.u8HasPps);

                /* 诊断：把能力报文的原始 32 位字打出来，用来核对档位类型。
                   型号看 bit[31:30]：00 固定、01 电池、10 可变、11 可调；
                   可调档再看子类型 bit[29:28]，PPS 是 00。
                   这段必须放在 Request 之后：前面几毫秒属于源端的响应窗口。 */
                {
                    char    acRaw[((uint16_t)DEV_PD_PDO_MAX * 9u) + 1u];
                    uint8_t u8Pos = 0u;
                    uint8_t u8Idx;

                    for (u8Idx = 0u; u8Idx < tCap.u8RawCount; u8Idx++)
                    {
                        u8Pos = (uint8_t)(u8Pos + (uint8_t)sprintf(&acRaw[u8Pos], "%08lX ",
                                                                   (unsigned long)tCap.au32Raw[u8Idx]));
                    }
                    acRaw[u8Pos] = '\0';
                    log_info("PD srccap raw %s", acRaw);
                }
            }
            else
            {
                log_warn("PD support: src cap has no fixed pdo");
            }
            break;

        case DEF_TYPE_ACCEPT:
            if (tLink.eState == E_DEV_PD_WAIT_ACCEPT)
            {
                tLink.eState = E_DEV_PD_WAIT_PS_RDY;
                tLink.u16Timer = 0u;
            }
            break;

        case DEF_TYPE_PS_RDY:
            if (tLink.eState == E_DEV_PD_WAIT_PS_RDY)
            {
                tLink.eState = E_DEV_PD_READY;
                tLink.u8ActiveIndex = tLink.u8ReqIndex;
                tLink.u16ActiveMv = tCap.atPdo[tLink.u8ReqIndex - 1u].u16MinVoltageMv;
                tLink.u8PendIndex = 0u;

                /* 档位表放在契约成立后再打：协商关键窗口内的每一毫秒都要留给报文 */
                for (i = 0u; i < tCap.u8Count; i++)
                {
                    log_info("  pdo %u: %u mV, %u mA",
                             (unsigned)tCap.atPdo[i].u8PdoIndex,
                             (unsigned)tCap.atPdo[i].u16MinVoltageMv,
                             (unsigned)tCap.atPdo[i].u16MaxCurrentMa);
                }
            }
            break;

        case DEF_TYPE_REJECT:
        case DEF_TYPE_WAIT:
            tLink.eState = E_DEV_PD_ERROR;                /* 对端不接受这个档位 */
            tLink.u8ErrCnt = 0u;
            break;

        case DEF_TYPE_SOFT_RESET:
            /* 对端要求软复位：回 Accept，然后重新等能力报文 */
            PdLoadHeader(0u, DEF_TYPE_ACCEPT);
            (void)PdSend(0u);
            tLink.eState = E_DEV_PD_WAIT_SRC_CAP;
            tLink.u16Timer = 0u;
            break;

        case DEF_TYPE_GET_SNK_CAP:
            /* 对端问我们的受电能力：现在没有真实档位表，直接拒 */
            PdLoadHeader(0u, DEF_TYPE_REJECT);
            (void)PdSend(0u);
            break;

        default:
            log_info("PD rx type %u len %u", (unsigned)u8Type, (unsigned)u8Len);
            break;                                  /* 其它类型先只记一笔，方便看对端发了什么 */
    }
}

/**
 * @brief  把上层挑好的档位发成 Request
 * @note   注意：本函数会往 CC 上发报文，是本板唯一会惊动源端的地方。
 */
static void PdTryRequest(void)
{
    const tDevPdPdoDef *ptPdo = 0;
    eStatusDef eStatus;
    uint32_t u32Rdo;
    uint16_t u16Current;
    uint8_t i;

    if (tLink.u8PendIndex == 0u)
    {
        return;
    }
    /* 等能力报文时可以请求；已经协商好之后再请求 = 换档 */
    if ((tLink.eState != E_DEV_PD_WAIT_SRC_CAP) && (tLink.eState != E_DEV_PD_READY))
    {
        return;
    }

    for (i = 0u; i < tCap.u8Count; i++)               /* 序号必须在解析出来的表里 */
    {
        if (tCap.atPdo[i].u8PdoIndex == tLink.u8PendIndex)
        {
            ptPdo = &tCap.atPdo[i];
            break;
        }
    }
    if (ptPdo == 0)
    {
        return;
    }

    /* 固定档 RDO：bit[31:28] 位置，bit25 USB communications capable，
     * bit24 No USB Suspend，bit[19:10] 工作电流，bit[9:0] 最大电流。
     * 官方 WCH Sink 例程的最高字节固定带 0x03；之前漏掉 bit25/bit24，
     * 部分充电器会在收到 Request 后直接复位供电。 */
    u16Current = (uint16_t)(ptPdo->u16MaxCurrentMa / 10u);
    u32Rdo  = ((uint32_t)tLink.u8PendIndex << 28);
    u32Rdo |= (1UL << 25);                            /* USB communications capable */
    u32Rdo |= (1UL << 24);                            /* No USB Suspend */
    u32Rdo |= ((uint32_t)u16Current & 0x3FFu);
    u32Rdo |= (((uint32_t)u16Current & 0x3FFu) << 10);

    PdLoadHeader(0u, DEF_TYPE_REQUEST);
    tBuf.au8Tx[2] = (uint8_t)(u32Rdo & 0xFFu);           /* 数据对象小端 */
    tBuf.au8Tx[3] = (uint8_t)((u32Rdo >> 8) & 0xFFu);
    tBuf.au8Tx[4] = (uint8_t)((u32Rdo >> 16) & 0xFFu);
    tBuf.au8Tx[5] = (uint8_t)((u32Rdo >> 24) & 0xFFu);

    /* 先把 Request 发出去，再打日志：这几毫秒属于源端的响应窗口，不能花在串口上 */
    eStatus = PdSend(4u);

    log_info("PD request pdo%u %umA rdo=%08lX tx=%02X %02X %02X %02X %02X %02X %s",
             (unsigned)tLink.u8PendIndex, (unsigned)ptPdo->u16MaxCurrentMa,
             (unsigned long)u32Rdo,
             (unsigned)tBuf.au8Tx[0], (unsigned)tBuf.au8Tx[1],
             (unsigned)tBuf.au8Tx[2], (unsigned)tBuf.au8Tx[3],
             (unsigned)tBuf.au8Tx[4], (unsigned)tBuf.au8Tx[5],
             (eStatus == E_OK) ? "ok" : "no GoodCRC");

    if (eStatus == E_OK)
    {
        tLink.u8ReqIndex = tLink.u8PendIndex;
        tLink.eState = E_DEV_PD_WAIT_ACCEPT;
        tLink.u16Timer = 0u;
        tLink.u8ErrCnt = 0u;
    }
    else
    {
        tLink.u8ErrCnt++;
        if (tLink.u8ErrCnt > (uint8_t)DEV_PD_REQ_TRY)
        {
            tLink.eState = E_DEV_PD_ERROR;
        }
    }
}

/* ---- 公共接口 ---- */

void DevPdInit(void)
{
    tLink.eState        = E_DEV_PD_IDLE;
    tLink.u8ActiveIndex = 0xFFu;
    tLink.u16ActiveMv   = 0u;
    tLink.u8PendIndex   = 0u;
    tLink.u8ReqIndex    = 0u;
    tLink.u8MsgId       = 0u;
    tLink.u16Timer      = 0u;
    tLink.u8ErrCnt      = 0u;

    tCap.u8Count        = 0u;

    tDet.u8Connected    = 0u;
    tDet.u8DetDiv       = 0u;
    tDet.u8DetCnt       = 0u;

    BspPdPhyInit();                                 /* PHY 的初始化由本层负责 */
}

void DevPdService(void)
{
    uint8_t u8Len = 0u;
    uint8_t u8Which;

    /* 1) 有帧就取走处理 */
    if (BspPdTakeRx(tBuf.au8Rx, (uint8_t)sizeof(tBuf.au8Rx), &u8Len) == E_OK)
    {
        PdHandleMsg(u8Len);
    }

    /* 2) 超时 */
    tLink.u16Timer++;
    if (tLink.eState == E_DEV_PD_WAIT_SRC_CAP)
    {
        /* 只有"连能力报文都还没收到"的时候才需要催和超时。
           一旦拿到表，Request 已经在 PdHandleMsg() 里当场发出去了，这里不再插手。 */
        if (tCap.u8Count == 0u)
        {
            if (tLink.u16Timer > (uint16_t)DEV_PD_T_SRCCAP)
            {
                tLink.u8ErrCnt++;
                tLink.u16Timer = 0u;
                log_warn("PD no src cap, try %u", (unsigned)tLink.u8ErrCnt);

                if (tLink.u8ErrCnt > (uint8_t)DEV_PD_REQ_TRY)
                {
                    tLink.eState = E_DEV_PD_ERROR;
                }
                else
                {
                    /* 板子带电复位时，源端认为受电端一直没拔过，上一轮契约还有效，
                       所以它不会主动重发能力报文，只复位本地 PHY 是永远等不到的。
                       受电端要重新同步，标准做法是发 Soft Reset：源端回 Accept，
                       紧接着重发一份能力报文，之后由 PdHandleMsg() 当场发 Request 接上。 */
                    PdLoadHeader(0u, DEF_TYPE_SOFT_RESET);
                    if (PdSend(0u) != E_OK)
                    {
                        BspPdPhyReset();            /* 连 Soft Reset 都发不出去：本地 PHY 重来 */
                    }
                }
            }
        }
    }
    else if ((tLink.eState == E_DEV_PD_WAIT_ACCEPT) || (tLink.eState == E_DEV_PD_WAIT_PS_RDY))
    {
        if (tLink.u16Timer > (uint16_t)DEV_PD_T_ACCEPT)
        {
            tLink.eState = E_DEV_PD_ERROR;                /* 对端不理我们 */
        }
    }
    else
    {
        /* 其它状态不判超时 */
    }

    /* 3) 该发请求就发 */
    PdTryRequest();

    /* 4) 连接检测：没插上时勤查（快点连上），插上后低频查（只为发现拔线）。
     *    检测会改动 CC 的比较器配置，所以插上时每次查完都要恢复回去。 */
    tDet.u8DetDiv++;
    if (tDet.u8DetDiv >= ((tDet.u8Connected == 0u) ? (uint8_t)DEV_PD_DET_DIV : (uint8_t)DEV_PD_DET_LINK))
    {
        tDet.u8DetDiv = 0u;
        u8Which = BspPdDetectCc();

        if (u8Which != 0u)
        {
            if (tDet.u8Connected == 0u)
            {
                if (tDet.u8DetCnt < (uint8_t)DEV_PD_DET_CONFIRM)
                {
                    tDet.u8DetCnt++;
                }

                if (tDet.u8DetCnt >= (uint8_t)DEV_PD_DET_CONFIRM)
                {
                    BspPdSelectCc(u8Which);         /* 定下走哪一路 CC */
                    tDet.u8Connected   = 1u;
                    tCap.u8Count    = 0u;
                    tLink.u8ActiveIndex = 0xFFu;
                    tLink.u16ActiveMv   = 0u;
                    tLink.u8PendIndex   = 0u;
                    tLink.u8ErrCnt      = 0u;
                    tLink.u16Timer      = 0u;
                    tLink.eState        = E_DEV_PD_WAIT_SRC_CAP;
                }
            }
            else
            {
                BspPdSelectCc(u8Which);             /* 检测动过 CC 配置，恢复回通信用的那套 */
            }
        }
        else
        {
            tDet.u8DetCnt = 0u;
            if (tDet.u8Connected != 0u)                  /* 拔掉了 */
            {
                tDet.u8Connected    = 0u;
                tLink.u8ActiveIndex = 0xFFu;
                tLink.u16ActiveMv   = 0u;
                tLink.eState        = E_DEV_PD_IDLE;
                BspPdPhyReset();
            }
        }
    }
}

eStatusDef DevPdRequestPdo(uint8_t u8PdoIndex)
{
    uint8_t i;

    if ((u8PdoIndex == 0u) || (u8PdoIndex > (uint8_t)DEV_PD_PDO_MAX))
    {
        return E_ERROR;
    }
    if (tDet.u8Connected == 0u)
    {
        return E_ERROR;
    }

    for (i = 0u; i < tCap.u8Count; i++)               /* 必须是已经解析到的档位 */
    {
        if (tCap.atPdo[i].u8PdoIndex == u8PdoIndex)
        {
            tLink.u8PendIndex = u8PdoIndex;               /* 记下来，由 DevPdService 发出去 */
            return E_OK;
        }
    }

    return E_ERROR;
}

void DevPdRestart(void)
{
    tCap.u8Count        = 0u;
    tCap.u8CountOld     = 0u;
    tCap.u8HasPpsOld    = 0u;

    tLink.u8PendIndex   = 0u;
    tLink.u8ReqIndex    = 0u;
    tLink.u8ErrCnt      = 0u;
    tLink.u16Timer      = 0u;
    tLink.u8ActiveIndex = 0xFFu;
    tLink.u16ActiveMv   = 0u;
    tLink.eState        = (tDet.u8Connected != 0u) ? E_DEV_PD_WAIT_SRC_CAP : E_DEV_PD_IDLE;

    BspPdPhyReset();
}

eDevPdStateDef DevPdGetState(void)
{
    return tLink.eState;
}

uint8_t DevPdGetPdoCount(void)
{
    return tCap.u8Count;
}

uint8_t DevPdHasPps(void)
{
    return tCap.u8HasPps;
}

const tDevPdPdoDef *DevPdGetPdo(uint8_t u8Index)
{
    if (u8Index >= tCap.u8Count)
    {
        return 0;
    }
    return &tCap.atPdo[u8Index];
}

uint8_t DevPdGetActiveIndex(void)
{
    return tLink.u8ActiveIndex;
}

uint16_t DevPdGetActiveMv(void)
{
    return tLink.u16ActiveMv;
}

uint8_t DevPdIsConnected(void)
{
    return tDet.u8Connected;
}
