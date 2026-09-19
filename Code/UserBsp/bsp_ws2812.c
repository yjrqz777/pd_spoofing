#include "bsp_ws2812.h"

/*
 * PWM 槽位缓冲：每颗灯珠 24 个槽（GRB 位），尾部 RESET_LEN 个槽输出常低，构成复位间隔。
 * 槽位内容 = TIM1_CH1 的比较值（CODE_0 / CODE_1）。
 */
static uint16_t u16ColorSlot[COLOR_BUFFER_LEN] = {0};

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

    DMA_DeInit(TIM_DMA_CH1_CH);
    DMA_Cmd(TIM_DMA_CH1_CH, DISABLE);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t) &TIM1->CH1CVR;
    DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t) u16ColorSlot;
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralDST;
    DMA_InitStructure.DMA_BufferSize = COLOR_BUFFER_LEN;
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;
    DMA_InitStructure.DMA_Priority = DMA_Priority_VeryHigh;
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(TIM_DMA_CH1_CH, &DMA_InitStructure);

    DMA_Cmd(TIM_DMA_CH1_CH, DISABLE);
    DMA_ITConfig( DMA1_Channel5, DMA_IT_TC, ENABLE);

    NVIC_InitStructure.NVIC_IRQChannel = DMA1_Channel5_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    // DMA_Cmd(TIM_DMA_CH1_CH, ENABLE);
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
    if (DMA_GetFlagStatus(DMA1_FLAG_TC5)) 
    {
        TIM_Cmd(TIM1, DISABLE);   /* 停的是整个 TIM1：今后 CH2/CH3 若共用，需改为只关本通道 */
        DMA_Cmd(TIM_DMA_CH1_CH, DISABLE);
        DMA_ClearFlag(DMA1_FLAG_TC5);
    }
}



/* 把一段 GRB 字节流编码成 PWM 槽位写进缓冲（8 bit -> 8 个 compare） */
uint8_t BspWs2812EncodeBytes(const uint8_t *bytes, uint16_t len)
{

}

eStatusDef BspWs2812Show(void)
{

}

uint8_t BspWs2812IsIdle(void)
{

}








