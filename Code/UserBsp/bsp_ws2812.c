#include "bsp_ws2812.h"

uint16_t color_buf[COLOR_BUFFER_LEN] = {0};

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
    DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t) color_buf;
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

    DMA_Cmd(TIM_DMA_CH1_CH, ENABLE);
}



void BspWs2812Init(void)
{
    Ws2812GpioInit();
    Ws2812Time1OC1Init();
    Ws2812DmaInit();
    
}


void DMA1_Channel5_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void DMA1_Channel5_IRQHandler(void)
{
    if (DMA_GetFlagStatus(DMA1_FLAG_TC5)) 
    {
        TIM_Cmd(TIM1, DISABLE);
        DMA_Cmd(TIM_DMA_CH1_CH, DISABLE);
        DMA_ClearFlag(DMA1_FLAG_TC5);
    }
}