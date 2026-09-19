#include "bsp_ws2812.h"

/*
 * PWM 槽位缓冲：每颗灯珠 24 个槽（GRB 位），尾部 RESET_LEN 个槽输出常低，构成复位间隔。
 * 槽位内容 = TIM1_CH1 的比较值（CODE_0 / CODE_1）。
 */
static uint16_t u16ColorSlot[WS2812_SLOT_NUM] = {0};

/** @brief 发送状态：1=空闲（可装载、可启动），0=上一帧还在发送 */
static volatile uint8_t s_u8DmaIdle = 1u;


static void Ws2812GpioInit(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    memset(&GPIO_InitStructure, 0, sizeof(GPIO_InitStructure));

    GPIO_InitStructure.GPIO_Pin = WS2812_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(WS2812_PORT, &GPIO_InitStructure);
}

static void Ws2812Time1OC1Init(void)
{
    TIM_OCInitTypeDef TIM_OCInitStructure;
    memset(&TIM_OCInitStructure, 0, sizeof(TIM_OCInitStructure));

    TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_Pulse = 0;
    TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OC1Init(TIM1, &TIM_OCInitStructure);

    TIM_OC1PreloadConfig(TIM1, TIM_OCPreload_Enable);
    TIM_CtrlPWMOutputs(TIM1, ENABLE);
    TIM_DMACmd(TIM1, TIM_DMA_Update, ENABLE);
}

static void Ws2812DmaInit(void)
{
    DMA_InitTypeDef DMA_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;
    memset(&DMA_InitStructure, 0, sizeof(DMA_InitStructure));
    memset(&NVIC_InitStructure, 0, sizeof(NVIC_InitStructure));

    RCC_AHBPeriphClockCmd( RCC_AHBPeriph_DMA1, ENABLE);

    DMA_DeInit(WS2812_DMA_CH);
    DMA_Cmd(WS2812_DMA_CH, DISABLE);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t) &TIM1->CH1CVR;
    DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t) u16ColorSlot;
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralDST;
    DMA_InitStructure.DMA_BufferSize = WS2812_SLOT_NUM;
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;
    DMA_InitStructure.DMA_Priority = DMA_Priority_VeryHigh;
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(WS2812_DMA_CH, &DMA_InitStructure);

    DMA_Cmd(WS2812_DMA_CH, DISABLE);
    /* Transfer Complete && Transfer Error*/
    DMA_ITConfig( DMA1_Channel5, DMA_IT_TC | DMA_IT_TE, ENABLE);

    NVIC_InitStructure.NVIC_IRQChannel = DMA1_Channel5_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    // DMA_Cmd(WS2812_DMA_CH, ENABLE);
}



void BspWs2812Init(void)
{
    BspTim1BaseInit();                                    /* TIM1 时基(PSC/ARR/启动)统一由 bsp_time.c 配置 */
    Ws2812GpioInit();
    Ws2812Time1OC1Init();
    Ws2812DmaInit();
    
}


void DMA1_Channel5_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void DMA1_Channel5_IRQHandler(void)
{
    if (DMA_GetFlagStatus(DMA1_FLAG_TE5) != RESET)   /* 传输错误：必须同样收尾，否则忙标志永远不复位 */
    {
        DMA_ClearFlag(DMA1_FLAG_TE5);
    }

    if (DMA_GetFlagStatus(DMA1_FLAG_TC5) != RESET)   /* 一帧发完 */
    {
        DMA_ClearFlag(DMA1_FLAG_TC5);
    }

    DMA_Cmd(WS2812_DMA_CH, DISABLE);                /* 只停本通道 DMA，不动 TIM1 */
    TIM_SetCompare1(TIM1, 0u);                       /* 输出回低电平，避免停在最后一个高电平槽 */
    s_u8DmaIdle = 1u;                                /* 允许下一帧 */
}



/* 把一段 GRB 字节流编码成 PWM 槽位写进缓冲（8 bit -> 8 个 compare） */
static void Ws2812EncodeByte(uint16_t *pu16Dest, uint8_t u8Value)
{
    uint8_t BitIndex;

    for (BitIndex = 0u; BitIndex < 8u; BitIndex++)
    {
        pu16Dest[BitIndex] = ((u8Value & (uint8_t)(0x80u >> BitIndex)) != 0u) ? WS2812_CODE_1 : WS2812_CODE_0;
    }
}


/**
 * @brief  把一段字节流编码进 PWM 槽位缓冲，供 BspWs2812Show() 发送
 * @param[in] pu8Bytes 字节流，GRB 顺序，长度 = 灯珠数 x 3
 * @param[in] u16Len   字节数
 * @retval E_OK    已装载
 * @retval E_BUSY  上一帧还在发送，本次未装载（缓冲不可被打断）
 * @retval E_ERROR 参数为空或超出缓冲容量
 * @note   尾部 RESET_LEN 个复位槽始终保持 0，由本函数之外的数据决定。
 */
eStatusDef BspWs2812LoadBytes(const uint8_t *pu8Bytes, uint16_t u16Len)
{
    uint16_t  u16ByteIndex;
    uint16_t *pu16Slot;

    if ((pu8Bytes == 0) || (u16Len == 0u) || (u16Len > (uint16_t)(WS2812_PIXEL_NUM * 3)))
    {
        return E_ERROR;
    }

    if (s_u8DmaIdle == 0u)
    {
        return E_BUSY;                       /* 发送中：不碰缓冲，避免波形撕裂 */
    }

    pu16Slot = u16ColorSlot;
    for (u16ByteIndex = 0u; u16ByteIndex < u16Len; u16ByteIndex++)
    {
        Ws2812EncodeByte(pu16Slot, pu8Bytes[u16ByteIndex]);
        pu16Slot += 8u;                      /* 每字节占 8 个槽位 */
    }

    return E_OK;
}

/**
 * @brief  Starts DMA transfer of the loaded slot frame.
 * @retval E_OK   The transfer was started.
 * @retval E_BUSY A previous frame is still in flight.
 * @note   Restarts the counter and CH1 compare value so the frame begins at a
 *         known slot, and re-enables TIM1 because the transfer-complete ISR
 *         disables the whole timer.
 */
eStatusDef BspWs2812Show(void)
{
    if (s_u8DmaIdle == 0u)
    {
        return E_BUSY;                            /* 上一帧还没发完 */
    }

    s_u8DmaIdle = 0u;                             /* 先占位：期间 LoadBytes 会被拒绝，避免撕裂波形 */

    TIM_Cmd(TIM1, DISABLE);                       /* 停计数器，保证从槽 0 开始发 */
    DMA_Cmd(WS2812_DMA_CH, DISABLE);
    DMA_ClearFlag(DMA1_FLAG_TC5);                 /* 清掉上一次的完成标志 */
    DMA_SetCurrDataCounter(WS2812_DMA_CH, WS2812_SLOT_NUM);
    WS2812_DMA_CH->MADDR = (uint32_t)u16ColorSlot; /* 重装源地址，防止计数跑偏后指针漂移 */
    TIM_SetCounter(TIM1, 0u);
    TIM_SetCompare1(TIM1, 0u);                    /* 复位瞬间输出低电平 */
    DMA_Cmd(WS2812_DMA_CH, ENABLE);              /* 先备好 DMA... */
    TIM_Cmd(TIM1, ENABLE);                        /* ...再开 TIM1，请求才开始产生 */

    return E_OK;
}

/**
 * @brief  Reports whether the slot buffer is free for loading and starting a new frame.
 * @retval 1 Idle: LoadBytes and Show may be called.
 * @retval 0 Busy: a frame is still in flight.
 */
uint8_t BspWs2812IsIdle(void)
{
    return s_u8DmaIdle;
}







