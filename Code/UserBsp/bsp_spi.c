/**
 * @file bsp_spi.c
 * @brief Implements the LCD SPI1 and transmit-DMA interface.
 * @details SPI1 uses PA5 as SCK and PA7 as MOSI in one-line transmit Mode 2.
 *          DMA1 channel 3 transfers LCD pixel buffers asynchronously.
 */

#include "bsp_spi.h"
#include "ch32x035_dma.h"
#include "ch32x035_gpio.h"
#include "ch32x035_misc.h"
#include "ch32x035_rcc.h"
#include "ch32x035_spi.h"

static volatile uint8_t u8SpiDmaIdle = 1u;
static volatile uint8_t u8SpiError = 0u;

/**
 * @brief Initializes SPI1 Mode 2 and DMA1 channel 3 for LCD transmission.
 */
void BspSpiInit(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    memset(&GPIO_InitStructure, 0, sizeof(GPIO_InitStructure));

#if LCD_IO_STATIC_TEST_ENABLE
    uint32_t LcdPinMask;

    LcdPinMask = LCD_RES_PIN | LCD_DC_PIN | LCD_CS_PIN |
                 LCD_SCK_PIN | LCD_SDA_PIN;

#if LCD_IO_STATIC_TEST_LEVEL
    GPIO_SetBits(GPIOA, LcdPinMask);
#else
    GPIO_ResetBits(GPIOA, LcdPinMask);
#endif

    GPIO_InitStructure.GPIO_Pin = LcdPinMask;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

#if LCD_IO_STATIC_TEST_LEVEL
    GPIO_SetBits(GPIOA, LcdPinMask);
#else
    GPIO_ResetBits(GPIOA, LcdPinMask);
#endif

    printf("[LCD-IO-TEST] RES/DC/CS/SCK/SDA forced %s\r\n",
           (LCD_IO_STATIC_TEST_LEVEL != 0u) ? "HIGH" : "LOW");
#else
    SPI_InitTypeDef SPI_InitStructure;
    DMA_InitTypeDef DMA_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    memset(&SPI_InitStructure, 0, sizeof(SPI_InitStructure));
    memset(&DMA_InitStructure, 0, sizeof(DMA_InitStructure));
    memset(&NVIC_InitStructure, 0, sizeof(NVIC_InitStructure));
    u8SpiDmaIdle = 1u;
    u8SpiError = 0u;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_SPI1, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    GPIO_SetBits(LCD_SCK_PORT, LCD_SCK_PIN);
    GPIO_ResetBits(LCD_SDA_PORT, LCD_SDA_PIN);
    GPIO_InitStructure.GPIO_Pin = LCD_SCK_PIN | LCD_SDA_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(LCD_SCK_PORT, &GPIO_InitStructure);

    SPI_InitStructure.SPI_Direction = SPI_Direction_1Line_Tx;
    SPI_InitStructure.SPI_Mode = SPI_Mode_Master;
    SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;
    SPI_InitStructure.SPI_CPOL = SPI_CPOL_High;
    SPI_InitStructure.SPI_CPHA = SPI_CPHA_1Edge;
    SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_4;
    SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;
    SPI_InitStructure.SPI_CRCPolynomial = 7u;
    SPI_Init(SPI1, &SPI_InitStructure);
    SPI_NSSInternalSoftwareConfig(SPI1, SPI_NSSInternalSoft_Set);
    SPI_Cmd(SPI1, ENABLE);

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
    DMA_ITConfig(DMA1_Channel3, DMA_IT_TC | DMA_IT_TE, ENABLE);

    NVIC_InitStructure.NVIC_IRQChannel = DMA1_Channel3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1u;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0u;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, DISABLE);

    printf("[SPI] SPI1 ready: 1-line TX mode2 /4 + DMA1_CH3, SCK=PA5 MOSI=PA7\r\n");
    printf("[SPI] CTLR1=%04x CTLR2=%04x STATR=%04x\r\n",
           (unsigned int)SPI1->CTLR1, (unsigned int)SPI1->CTLR2,
           (unsigned int)SPI1->STATR);
#endif
}

/**
 * @brief Sends one byte through SPI1 using polling.
 * @param[in] u8Data Byte to send, most significant bit first.
 */
void BspSpiWriteByte(uint8_t u8Data)
{
#if LCD_IO_STATIC_TEST_ENABLE
    (void)u8Data;
#else
    uint32_t Timeout;

    if (u8SpiError != 0u)
    {
        return;
    }

    Timeout = BSP_SPI_TIMEOUT_COUNT;
    while ((u8SpiDmaIdle == 0u) && (Timeout != 0u))
    {
        Timeout--;
    }

    while ((SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET) &&
           (Timeout != 0u))
    {
        Timeout--;
    }

    if (Timeout == 0u)
    {
        u8SpiError = 1u;
        return;
    }

    SPI_I2S_SendData(SPI1, u8Data);

    Timeout = BSP_SPI_TIMEOUT_COUNT;
    while ((SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY) != RESET) &&
           (Timeout != 0u))
    {
        Timeout--;
    }

    if (Timeout == 0u)
    {
        u8SpiError = 1u;
    }
#endif
}

/**
 * @brief Sends a byte buffer through SPI1 using polling.
 * @param[in] pu8Data Pointer to the source buffer.
 * @param[in] u16Len Number of bytes to send.
 */
void BspSpiWriteBufferBlocking(const uint8_t *pu8Data, uint16_t u16Len)
{
    uint16_t Index;

    if (pu8Data == 0)
    {
        return;
    }

    for (Index = 0u; Index < u16Len; Index++)
    {
        BspSpiWriteByte(pu8Data[Index]);
    }
}

/**
 * @brief Starts an asynchronous SPI1 transmit using DMA1 channel 3.
 * @param[in] pu8Data Pointer to data that remains valid until completion.
 * @param[in] u16Len Number of bytes to transmit.
 * @retval E_OK The transfer was started.
 * @retval E_BUSY A previous DMA transfer is active.
 * @retval E_ERROR The parameters or SPI state are invalid.
 */
eStatusDef BspSpiWriteBufferDma(const uint8_t *pu8Data, uint16_t u16Len)
{
#if LCD_IO_STATIC_TEST_ENABLE
    (void)pu8Data;
    (void)u16Len;
    return E_ERROR;
#else
    if ((pu8Data == 0) || (u16Len == 0u) || (u8SpiError != 0u))
    {
        return E_ERROR;
    }

    if (u8SpiDmaIdle == 0u)
    {
        return E_BUSY;
    }

    u8SpiDmaIdle = 0u;
    DMA_Cmd(DMA1_Channel3, DISABLE);
    DMA_SetCurrDataCounter(DMA1_Channel3, u16Len);
    DMA1_Channel3->MADDR = (uint32_t)pu8Data;
    DMA_ClearITPendingBit(DMA1_IT_GL3);
    SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, ENABLE);
    DMA_Cmd(DMA1_Channel3, ENABLE);

    return E_OK;
#endif
}

/**
 * @brief Reports whether the transmit DMA channel is idle.
 * @retval 1 No DMA transfer is active.
 * @retval 0 A DMA transfer is active.
 */
uint8_t BspSpiIsIdle(void)
{
    return u8SpiDmaIdle;
}

/**
 * @brief Reports whether an SPI or DMA error occurred.
 * @retval 1 An error or timeout occurred.
 * @retval 0 No error occurred.
 */
uint8_t BspSpiHasError(void)
{
    return u8SpiError;
}

/**
 * @brief Handles SPI1 transmit completion and errors from DMA1 channel 3.
 */
void DMA1_Channel3_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void DMA1_Channel3_IRQHandler(void)
{
    uint32_t Timeout;

    if (DMA_GetITStatus(DMA1_IT_TE3) != RESET)
    {
        DMA_ClearITPendingBit(DMA1_IT_GL3);
        DMA_Cmd(DMA1_Channel3, DISABLE);
        SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, DISABLE);
        u8SpiError = 1u;
        u8SpiDmaIdle = 1u;
        return;
    }

    if (DMA_GetITStatus(DMA1_IT_TC3) != RESET)
    {
        DMA_ClearITPendingBit(DMA1_IT_GL3);
        DMA_Cmd(DMA1_Channel3, DISABLE);
        SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, DISABLE);

        Timeout = BSP_SPI_TIMEOUT_COUNT;
        while ((SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY) != RESET) &&
               (Timeout != 0u))
        {
            Timeout--;
        }

        if (Timeout == 0u)
        {
            u8SpiError = 1u;
        }
        u8SpiDmaIdle = 1u;
    }
}
