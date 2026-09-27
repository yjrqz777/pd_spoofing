
#include "bsp_spi.h"

static volatile uint8_t u8SpiDmaIdle = 1u;  /* 是否空闲 */
static volatile uint8_t u8SpiError = 0u;    /* ERROR */ 


static void Spi1GPIOInit(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    memset(&GPIO_InitStructure, 0, sizeof(GPIO_InitStructure));

    GPIO_SetBits(LCD_SCK_PORT, LCD_SCK_PIN);
    GPIO_ResetBits(LCD_SDA_PORT, LCD_SDA_PIN);

    GPIO_InitStructure.GPIO_Pin = LCD_SCK_PIN | LCD_SDA_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(LCD_SCK_PORT, &GPIO_InitStructure);


    GPIO_InitStructure.GPIO_Pin = LCD_RES_PIN | LCD_DC_PIN | LCD_CS_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(LCD_RES_PORT, &GPIO_InitStructure);

}



static void Spi1Init(void)
{
    SPI_InitTypeDef SPI_InitStructure;
    memset(&SPI_InitStructure, 0, sizeof(SPI_InitStructure));

    SPI_InitStructure.SPI_Direction = SPI_Direction_1Line_Tx;
    SPI_InitStructure.SPI_Mode = SPI_Mode_Master;
    SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;
    SPI_InitStructure.SPI_CPOL = SPI_CPOL_High;
    SPI_InitStructure.SPI_CPHA = SPI_CPHA_1Edge;
    SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_2;
    SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;
    SPI_InitStructure.SPI_CRCPolynomial = 7u;
    SPI_Init(SPI1, &SPI_InitStructure);
    SPI_NSSInternalSoftwareConfig(SPI1, SPI_NSSInternalSoft_Set);
    SPI_I2S_ITConfig(SPI1, SPI_I2S_IT_TXE, DISABLE);
    SPI_Cmd(SPI1, ENABLE);
}

static void Spi1DmaInit(void)
{
    DMA_InitTypeDef DMA_InitStructure;
    memset(&DMA_InitStructure, 0, sizeof(DMA_InitStructure));

    DMA_DeInit(DMA1_Channel3);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&SPI1->DATAR;
    DMA_InitStructure.DMA_MemoryBaseAddr = 0u;
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralDST;
    DMA_InitStructure.DMA_BufferSize = 0u;
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;
    DMA_InitStructure.DMA_Priority = DMA_Priority_High;
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel3, &DMA_InitStructure);
    DMA_ClearITPendingBit(DMA1_IT_GL3);
    // DMA_ITConfig(DMA1_Channel3, DMA_IT_TC | DMA_IT_TE, ENABLE);
    SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, DISABLE);
}

static void Spi1NVICInit(void)
{
    NVIC_InitTypeDef NVIC_InitStructure;
    memset(&NVIC_InitStructure, 0, sizeof(NVIC_InitStructure));

    NVIC_InitStructure.NVIC_IRQChannel = DMA1_Channel3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1u;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0u;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}







void BspSpiInit(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    Spi1GPIOInit();
    Spi1Init();
    Spi1DmaInit();
    Spi1NVICInit();

    u8SpiDmaIdle = 1u;
    u8SpiError   = 0u;
}

void BspSpiRst(uint8_t u8Level)
{
    GPIO_WriteBit(LCD_RES_PORT, LCD_RES_PIN, (BitAction)u8Level);
}
void BspSpiDc(uint8_t u8Level)
{
    GPIO_WriteBit(LCD_DC_PORT, LCD_DC_PIN, (BitAction)u8Level);
}
void BspSpiCs(uint8_t u8Level)
{
    GPIO_WriteBit(LCD_CS_PORT, LCD_CS_PIN, (BitAction)u8Level);
}

void BspSpiSendByte(uint8_t u8Data)
{
    SPI_I2S_SendData(SPI1, u8Data);
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET)
    {
        /* Wait for TXE flag to be set */
    }
}

uint8_t BspSpiSendDmaStart(const uint8_t *pu8Data, uint16_t u16Len)
{
    if (pu8Data == NULL || u16Len == 0u)    
    {
        return 1;
    }
    if ((u8SpiDmaIdle == 0u) || (u8SpiError != 0u)) /* 忙或已锁死 */
    { 
        return 1; 
    }   

    u8SpiDmaIdle = 0u; /* 标记为忙 */
    DMA_Cmd(DMA1_Channel3, DISABLE);
    DMA_ClearFlag(DMA1_FLAG_TC3 | DMA1_FLAG_TE3 | DMA1_IT_GL3);
    DMA_SetCurrDataCounter(DMA1_Channel3, u16Len);
    DMA1_Channel3->MADDR = (uint32_t)pu8Data;
    SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, ENABLE);
    DMA_Cmd(DMA1_Channel3, ENABLE);
    return 0;
}


void BspSpiDmaService(void)
{
    uint32_t Timeout = 10000u;
    if (u8SpiDmaIdle != 0u)/* 空闲返回 */
    {
        return;
    }
    
    if (DMA_GetFlagStatus(DMA1_FLAG_TC3) == SET)
    {

        DMA_ClearFlag(DMA1_FLAG_TC3);

        while ((SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY) == SET) && (Timeout != 0u))
        {
            Timeout--;
        }
        if (Timeout == 0u) 
        { 
            u8SpiError = 1u; 
        }

        DMA_Cmd(DMA1_Channel3, DISABLE);
        SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, DISABLE);
        u8SpiDmaIdle = 1u;
    }
    else if (DMA_GetFlagStatus(DMA1_FLAG_TE3) == SET)/* 发送 错误 处理 */
    {
        DMA_ClearFlag(DMA1_FLAG_TE3);
        DMA_Cmd(DMA1_Channel3, DISABLE);
        SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, DISABLE);
        u8SpiDmaIdle = 1u;
        u8SpiError = 1u;
    }
}






uint8_t BspSpiDmaIsIdle(void)
{
    return u8SpiDmaIdle;
}
uint8_t BspSpiDmaHasError(void)
{
    return u8SpiError;
}
