

#include "bsp_pdsink.h"

/* ---- 模块内部状态（按用途分成两组） ---- */

/** @brief PHY 收发缓冲
 *  @note  USBPD->DMA 装的就是这几块地址，每个成员都必须 4 字节对齐。 */
typedef struct
{
    uint8_t au8Rx[BSP_PD_FRAME_MAX] __attribute__((aligned(4)));  /* 接收缓冲 */
    uint8_t au8Tx[BSP_PD_FRAME_MAX] __attribute__((aligned(4)));  /* 发送缓冲 */
    uint8_t au8Ack[2]               __attribute__((aligned(4)));  /* GoodCRC 应答缓冲 */
} tBspPdBufDef;

/** @brief PHY 运行标志：中断与主循环之间靠这几个字节握手，都要 volatile */
typedef struct
{
    volatile uint8_t u8RxLen;     /* 上一帧的字节数（含尾部 CRC） */
    volatile uint8_t u8RxReady;   /* 1 = 接收缓冲里有一帧等上层取 */
    volatile uint8_t u8AckPend;   /* 1 = 这次发送是给对端回的 GoodCRC */
} tBspPdFlagDef;

static tBspPdBufDef  tBuf;    /* 收发缓冲 */
static tBspPdFlagDef tFlag;   /* 运行标志 */



/** @brief 进接收模式：清标志、开接收中断、DMA 指回接收缓冲、启动 BMC */
static void BspPdRxMode(void)
{
    USBPD->CONFIG |= PD_ALL_CLR;                    /* 置 1 再清 0：把所有中断标志清一遍 */
    USBPD->CONFIG &= ~PD_ALL_CLR;
    USBPD->CONFIG &= ~IE_TX_END;                    /* 发送完成中断平时不开，只在回 GoodCRC 前临时开 */
    USBPD->CONFIG &= ~IE_RX_RESET;                  /* 复位中断也不开：CC 悬空时它会连着重触发，
                                                       中断风暴会把主循环堵死，最后被看门狗复位 */
    USBPD->CONFIG |= IE_RX_ACT | PD_DMA_EN;         /* 只要"收完一帧"这一个中断 */

    USBPD->PORT_CC1 &= ~CC_LVE;                     /* 放掉 CC 拉低，上一次发送可能没放干净 */
    USBPD->PORT_CC2 &= ~CC_LVE;

    USBPD->DMA         = (uint32_t)(uint8_t *)tBuf.au8Rx;   /* 接收缓冲地址，硬件把收到的字节写这里 */
    USBPD->CONTROL    &= ~PD_TX_EN;                 /* 收发方向切回"收" */
    USBPD->BMC_CLK_CNT = UPD_TMR_RX_48M;            /* BMC 位时序：48MHz 下的接收定时值 */
    USBPD->CONTROL    |= BMC_START;                 /* 启动 BMC 收发状态机，开始等起始位 */

    NVIC_EnableIRQ(USBPD_IRQn);                     /* 允许 PD 中断 */
}

/**
 * @brief  中断里用的短延时
 * @param[in] u32Us 微秒数
 * @note   Delay_Us() 会改写 SysTick->CMP 并启停 SysTick，主循环里也在用它。
 *         中断里直接调，会把主循环正在等的那个延时打断，让它永远等不到
 *         COUNTFLAG，就地卡死，最后被看门狗复位。所以这里把 SysTick 的配置
 *         存下来，用完原样恢复。
 */
static void BspPdIsrDelayUs(uint32_t u32Us)
{
    uint64_t u64CmpSave  = SysTick->CMP;
    uint32_t u32CtlrSave = SysTick->CTLR;

    Delay_Us(u32Us);

    SysTick->CTLR &= ~(1u << 0);                    /* 先停，再恢复配置 */
    SysTick->CMP   = u64CmpSave;
    SysTick->CTLR  = u32CtlrSave;
}


/**
 * @brief  启动一次发送，不等发完
 * @param[in] pu8Data 报文内容（不含 CRC，硬件自己补）
 * @param[in] u8Len   字节数
 * @param[in] u8Sop   UPD_SOP0 / UPD_SOP1 / UPD_SOP2 / UPD_HARD_RESET / UPD_CABLE_RESET
 * @note   收尾在 IF_TX_END 里做（GoodCRC 路径），或由调用方轮询（BspPdSend 路径）。
 *         先拷进 tBuf.au8Tx 再发：DMA 读的是自己的缓冲，调用方的栈变量随时可能被回收。
 */
static void BspPdTxStart(const uint8_t *pu8Data, uint8_t u8Len, uint8_t u8Sop)
{
    uint8_t i;

    for (i = 0u; i < u8Len; i++)
    {
        tBuf.au8Tx[i] = pu8Data[i];
    }

    /* 正在通信的那一路 CC 拉低，避免对端把它当空闲 */
    if ((USBPD->CONFIG & CC_SEL) != 0u)
    {
        USBPD->PORT_CC2 |= CC_LVE;                  /* CC_SEL = 1：当前走 CC2 */
    }
    else
    {
        USBPD->PORT_CC1 |= CC_LVE;                  /* CC_SEL = 0：当前走 CC1 */
    }

    USBPD->BMC_CLK_CNT = UPD_TMR_TX_48M;            /* BMC 位时序：48MHz 下的发送定时值 */
    USBPD->DMA         = (uint32_t)(uint8_t *)tBuf.au8Tx;   /* 发送缓冲地址，硬件从这儿取字节 */
    USBPD->TX_SEL      = u8Sop;                     /* 前置码类型：UPD_SOP0 = 普通报文 */
    USBPD->BMC_TX_SZ   = u8Len;                     /* 要发几个字节 */
    USBPD->CONTROL    |= PD_TX_EN;                  /* 收发方向切到"发" */
    USBPD->STATUS     &= BMC_AUX_INVALID;           /* 清掉上一次的 SOP 类型残留 */
    USBPD->CONTROL    |= BMC_START;                 /* 启动 BMC 状态机，开始发 */
}


/** @brief SINK 模式：CC 比较器阈值 0.66V，下拉打开（与官方例程/旧工程一致） */
static void BspPdSinkInit(void)
{
    USBPD->PORT_CC1 = CC_CMP_66 | CC_PD;
    USBPD->PORT_CC2 = CC_CMP_66 | CC_PD;
}
/* ---- 中断服务函数 ---- */

void USBPD_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void USBPD_IRQHandler(void)
{
    if (USBPD->STATUS & IF_RX_ACT)                  /* 一帧收完 */
    {
        uint8_t u8Sop;
        uint8_t u8Type;

        USBPD->STATUS |= IF_RX_ACT;                 /* 写 1 清标志 */

        u8Sop  = (uint8_t)(USBPD->STATUS & MASK_PD_STAT);   /* 这一帧的 SOP 类型 */
        u8Type = (uint8_t)(tBuf.au8Rx[0] & 0x1Fu);             /* 这一帧的消息类型 */

        if (u8Sop == (uint8_t)PD_RX_SOP0)
        {
            tFlag.u8RxLen = (uint8_t)USBPD->BMC_BYTE_CNT; /* 帧总长，含尾部 4 字节 CRC */

            /* 对端发来的本身就是 GoodCRC 时不再应答 */
            if ((tFlag.u8RxLen >= 6u) &&
                ((tFlag.u8RxLen != 6u) || (u8Type != (uint8_t)DEF_TYPE_GOODCRC)))
            {
                BspPdIsrDelayUs((uint32_t)BSP_PD_ACK_DELAY_US);   /* 等对端把接收窗口打开 */

                tBuf.au8Ack[0] = 0x41u;                                  /* GoodCRC 头 */
                tBuf.au8Ack[1] = (uint8_t)(tBuf.au8Rx[1] & 0x0Eu);         /* 回它的消息 ID */

                tFlag.u8AckPend = 1u;
                USBPD->CONFIG |= IE_TX_END;                            /* 只这一次开发送完成中断 */
                BspPdTxStart(tBuf.au8Ack, 2u, UPD_SOP0);
            }
            /* 收的是 GoodCRC 就什么也不做：硬件仍停在接收态，上层也不需要看它 */
        }
    }

    if (USBPD->STATUS & IF_TX_END)                  /* 上面那个 GoodCRC 发完了 */
    {
        /* 发完必须立刻放掉 CC 的低压驱动：官方例程与旧工程都在这里清。
           不清的话，CC 会被一直钳在发送电平上，源端会当成受电端拔出而切断 VBUS。 */
        USBPD->PORT_CC1 &= ~CC_LVE;
        USBPD->PORT_CC2 &= ~CC_LVE;

        USBPD->STATUS |= IF_TX_END;                 /* 写 1 清标志 */
        USBPD->CONFIG &= ~IE_TX_END;                /* 关掉，免得下次主动发送时误触发 */

        if (tFlag.u8AckPend != 0u)
        {
            tFlag.u8AckPend = 0u;
            tFlag.u8RxReady = 1u;                         /* 整帧已就绪，等上层来取 */
            NVIC_DisableIRQ(USBPD_IRQn);            /* 取走之前不再收，免得冲掉缓冲 */
        }
    }

    if (USBPD->STATUS & IF_RX_RESET)                /* 对端复位或断连 */
    {
        USBPD->STATUS |= IF_RX_RESET;               /* 写 1 清标志 */
        BspPdSinkInit();                            /* CC 重新配成 SNK，等对端重新来一轮 */
    }

    if (USBPD->STATUS & BUF_ERR)                    /* 缓冲或 DMA 出错：这一帧没收全 */
    {
        USBPD->STATUS |= BUF_ERR;
    }
}






void BspPdPhyInit(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    memset(&GPIO_InitStructure, 0, sizeof(GPIO_InitStructure));
    memset(&NVIC_InitStructure, 0, sizeof(NVIC_InitStructure));

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_USBPD, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = USB_PD_CC1_PIN | USB_PD_CC2_PIN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IN_FLOATING;
    GPIO_Init(USB_PD_CC_PORT, &GPIO_InitStructure);

    AFIO->CTLR |= USBPD_IN_HVT | USBPD_PHY_V33;     /* CC 引脚高阈值输入 + PHY 按 3.3V 供电 */

    /* 中断优先级在这里统一分配：WS2812 是 0，SPI 是 1，PD 排它们后面 */
    NVIC_InitStructure.NVIC_IRQChannel = USBPD_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2u;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0u;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    USBPD->CONFIG = PD_DMA_EN;                      /* 先全清，只留 DMA 使能，中断位后面进接收模式再配 */
    USBPD->STATUS = BUF_ERR | IF_RX_BIT | IF_RX_BYTE | IF_RX_ACT | IF_RX_RESET | IF_TX_END;   /* 写 1 清掉所有标志 */

    tFlag.u8RxLen   = 0u;
    tFlag.u8RxReady = 0u;
    tFlag.u8AckPend = 0u;

    BspPdSinkInit();
    BspPdRxMode();
}

void BspPdPhyReset(void)
{
    BspPdSinkInit();
    BspPdRxMode();
}

uint8_t BspPdDetectCc(void)
{
    uint8_t u8Ret = 0u;
    USBPD->PORT_CC2 &= ~(CC_CMP_Mask | PA_CC_AI);
    USBPD->PORT_CC2 |= CC_CMP_22;

    USBPD->PORT_CC1 &= ~(CC_CMP_Mask | PA_CC_AI);   /* 先清掉阈值位和模拟输入位 */
    USBPD->PORT_CC1 |= CC_CMP_22;                   /* 阈值切到 0.22V：源端的上拉会把它顶过去 */

    // Delay_Us(2);
    if ((USBPD->PORT_CC2 & PA_CC_AI) != 0u)
    {
        u8Ret = 2u;                             /* 只有 CC2 有 */
    }
    /* 两路都有：有些 A-to-C 线两路都带上拉，按 CC1 处理，跟例程一致 */
    if ((USBPD->PORT_CC1 & PA_CC_AI) != 0u)         /* PA_CC_AI = 比较器输出电平 */
    {
        u8Ret = 1u;                                 /* CC1 上有源端 */
    }

    return u8Ret;
}

void BspPdSelectCc(uint8_t u8Which)
{
    if (u8Which == 1u)
    {
        USBPD->CONFIG &= ~CC_SEL;                   /* CC_SEL = 0：通信走 CC1 */
    }
    else
    {
        USBPD->CONFIG |= CC_SEL;                    /* CC_SEL = 1：通信走 CC2 */
    }

    BspPdSinkInit();                                /* 比较器阈值恢复 0.66V + SNK 下拉 */
}

uint8_t BspPdHasRx(void)
{
    return tFlag.u8RxReady;
}

eStatusDef BspPdTakeRx(uint8_t *pu8Buf, uint8_t u8BufLen, uint8_t *pu8Len)
{
    uint8_t i;

    if ((pu8Buf == 0) || (pu8Len == 0))
    {
        return E_ERROR;
    }

    if (tFlag.u8RxReady == 0u)
    {
        return E_BUSY;
    }

    if ((tFlag.u8RxLen == 0u) || (u8BufLen < tFlag.u8RxLen))
    {
        return E_ERROR;
    }

    for (i = 0u; i < tFlag.u8RxLen; i++)
    {
        pu8Buf[i] = tBuf.au8Rx[i];
    }
    *pu8Len = tFlag.u8RxLen;

    tFlag.u8RxReady = 0u;
    tFlag.u8RxLen   = 0u;
    BspPdRxMode();                                 /* 取走了，重新开接收 */

    return E_OK;
}

eStatusDef BspPdSend(const uint8_t *pu8Data, uint8_t u8Len)
{
    uint8_t  u8Try;
    uint16_t u16Wait;

    if ((pu8Data == 0) || (u8Len == 0u) || (u8Len > BSP_PD_FRAME_MAX))
    {
        return E_ERROR;
    }
    if (tFlag.u8AckPend != 0u)                           /* 中断那边还在回 GoodCRC */
    {
        return E_BUSY;
    }

    for (u8Try = 0u; u8Try < (uint8_t)BSP_PD_TX_TRY; u8Try++)
    {
        NVIC_DisableIRQ(USBPD_IRQn);               /* 这一段自己轮询，不让中断插手 */

        BspPdTxStart(pu8Data, u8Len, UPD_SOP0);

        /* 一帧最长约 1ms；正常一定会发完，这里给个上限，别把主循环钉死 */
        u16Wait = 20000u;
        while (((USBPD->STATUS & IF_TX_END) == 0u) && (u16Wait != 0u))
        {
            u16Wait--;
        }
        if (u16Wait == 0u)
        {
            log_warn("PD phy TX_END timeout, try %u status=%02X",
                     (unsigned)(u8Try + 1u), (unsigned)USBPD->STATUS);
        }
        USBPD->STATUS |= IF_TX_END;                /* 写 1 清标志（超时也要清） */

        /* 切到接收，等对端的 GoodCRC */
        USBPD->PORT_CC1 &= ~CC_LVE;
        USBPD->PORT_CC2 &= ~CC_LVE;
        USBPD->CONFIG |= PD_ALL_CLR;
        USBPD->CONFIG &= ~PD_ALL_CLR;
        USBPD->CONTROL &= ~PD_TX_EN;
        USBPD->DMA = (uint32_t)(uint8_t *)tBuf.au8Rx;
        USBPD->BMC_CLK_CNT = UPD_TMR_RX_48M;
        USBPD->CONTROL |= BMC_START;

        u16Wait = 250u;                            /* 约 750us 的接收窗口 */
        while (u16Wait != 0u)
        {
            if ((USBPD->STATUS & IF_RX_ACT) != 0u)
            {
                USBPD->STATUS |= IF_RX_ACT;
                if ((USBPD->BMC_BYTE_CNT == 6u) &&             /* 控制报文的线上长度 */
                    ((tBuf.au8Rx[0] & 0x1Fu) == DEF_TYPE_GOODCRC))
                {
                    break;                             /* 对端确认了 */
                }
            }
            Delay_Us(3);
            u16Wait--;
        }

        BspPdRxMode();                             /* 不管成没成，都恢复接收模式 */

        if (u16Wait != 0u)
        {
            return E_OK;
        }

        log_warn("PD phy GoodCRC timeout try%u last len%u hdr=%02X %02X status=%02X",
                 (unsigned)(u8Try + 1u), (unsigned)USBPD->BMC_BYTE_CNT,
                 (unsigned)tBuf.au8Rx[0], (unsigned)tBuf.au8Rx[1],
                 (unsigned)USBPD->STATUS);
    }

    return E_ERROR;                                /* 重发完还没等到确认 */
}
