/**
 * @file bsp_spi.c
 * @brief Implements the LCD SPI1 and transmit-DMA interface.
 * @details SPI1 uses PA5 as SCK and PA7 as MOSI in one-line transmit Mode 2.
 *          DMA1 channel 3 transfers LCD pixel buffers asynchronously.
 */

#include "bsp_spi.h"

static volatile uint8_t u8SpiDmaIdle = 1u;
static volatile uint8_t u8SpiError = 0u;


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
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_16;
    SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;
    SPI_InitStructure.SPI_CRCPolynomial = 7u;
    SPI_Init(SPI1, &SPI_InitStructure);
    SPI_NSSInternalSoftwareConfig(SPI1, SPI_NSSInternalSoft_Set);
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
    DMA_ITConfig(DMA1_Channel3, DMA_IT_TC | DMA_IT_TE, ENABLE);
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
    // Spi1DmaInit();
    // Spi1NVICInit();
}

// /* LCD 硬件控制引脚
//  * 注意：本板 N114-2413THBIG01-H13 的背光 LEDK 硬件直接接地（常亮），无背光控制脚 */
// #define LCD_RST(x)  do { if (x) { GPIO_SetBits(LCD_RES_PORT, LCD_RES_PIN); } else { GPIO_ResetBits(LCD_RES_PORT, LCD_RES_PIN); } } while (0)
// #define LCD_DC(x)   do { if (x) { GPIO_SetBits(LCD_DC_PORT,  LCD_DC_PIN);  } else { GPIO_ResetBits(LCD_DC_PORT,  LCD_DC_PIN);  } } while (0)
// #define LCD_CS(x)   do { if (x) { GPIO_SetBits(LCD_CS_PORT,  LCD_CS_PIN);  } else { GPIO_ResetBits(LCD_CS_PORT,  LCD_CS_PIN);  } } while (0)

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

// /**
//  * @brief Sends a byte buffer through SPI1 using polling.
//  * @param[in] pu8Data Pointer to the source buffer.
//  * @param[in] u16Len Number of bytes to send.
//  */
// void BspSpiWriteBufferBlocking(const uint8_t *pu8Data, uint16_t u16Len)
// {
//     uint16_t Index;

//     if (pu8Data == 0)
//     {
//         return;
//     }

//     for (Index = 0u; Index < u16Len; Index++)
//     {
//         BspSpiWriteByte(pu8Data[Index]);
//     }
// }

// /**
//  * @brief Starts an asynchronous SPI1 transmit using DMA1 channel 3.
//  * @param[in] pu8Data Pointer to data that remains valid until completion.
//  * @param[in] u16Len Number of bytes to transmit.
//  * @retval E_OK The transfer was started.
//  * @retval E_BUSY A previous DMA transfer is active.
//  * @retval E_ERROR The parameters or SPI state are invalid.
//  */
// eStatusDef BspSpiWriteBufferDma(const uint8_t *pu8Data, uint16_t u16Len)
// {
// #if LCD_IO_STATIC_TEST_ENABLE
//     (void)pu8Data;
//     (void)u16Len;
//     return E_ERROR;
// #else
//     if ((pu8Data == 0) || (u16Len == 0u) || (u8SpiError != 0u))
//     {
//         return E_ERROR;
//     }

//     if (u8SpiDmaIdle == 0u)
//     {
//         return E_BUSY;
//     }

//     u8SpiDmaIdle = 0u;
//     DMA_Cmd(DMA1_Channel3, DISABLE);
//     DMA_SetCurrDataCounter(DMA1_Channel3, u16Len);
//     DMA1_Channel3->MADDR = (uint32_t)pu8Data;
//     DMA_ClearITPendingBit(DMA1_IT_GL3);
//     SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, ENABLE);
//     DMA_Cmd(DMA1_Channel3, ENABLE);

//     return E_OK;
// #endif
// }

// /**
//  * @brief Reports whether the transmit DMA channel is idle.
//  * @retval 1 No DMA transfer is active.
//  * @retval 0 A DMA transfer is active.
//  */
// uint8_t BspSpiIsIdle(void)
// {
//     return u8SpiDmaIdle;
// }

// /**
//  * @brief Reports whether an SPI or DMA error occurred.
//  * @retval 1 An error or timeout occurred.
//  * @retval 0 No error occurred.
//  */
// uint8_t BspSpiHasError(void)
// {
//     return u8SpiError;
// }

// /**
//  * @brief Handles SPI1 transmit completion and errors from DMA1 channel 3.
//  */
// void DMA1_Channel3_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
// void DMA1_Channel3_IRQHandler(void)
// {
//     uint32_t Timeout;

//     if (DMA_GetITStatus(DMA1_IT_TE3) != RESET)
//     {
//         DMA_ClearITPendingBit(DMA1_IT_GL3);
//         DMA_Cmd(DMA1_Channel3, DISABLE);
//         SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, DISABLE);
//         u8SpiError = 1u;
//         u8SpiDmaIdle = 1u;
//         return;
//     }

//     if (DMA_GetITStatus(DMA1_IT_TC3) != RESET)
//     {
//         DMA_ClearITPendingBit(DMA1_IT_GL3);
//         DMA_Cmd(DMA1_Channel3, DISABLE);
//         SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, DISABLE);

//         Timeout = BSP_SPI_TIMEOUT_COUNT;
//         while ((SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY) != RESET) &&
//                (Timeout != 0u))
//         {
//             Timeout--;
//         }

//         if (Timeout == 0u)
//         {
//             u8SpiError = 1u;
//         }
//         u8SpiDmaIdle = 1u;
//     }
// }
