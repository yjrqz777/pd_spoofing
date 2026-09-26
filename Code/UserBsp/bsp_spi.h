/**
 * @file bsp_spi.h
 * @brief Declares the LCD SPI1 and transmit-DMA interface.
 */

#ifndef __BSP_SPI_H__
#define __BSP_SPI_H__

#ifdef __cplusplus
extern "C" {
#endif
#include "user_global.h"



void BspSpiInit(void);
// void BspSpiWriteByte(uint8_t u8Data);
// void BspSpiWriteBufferBlocking(const uint8_t *pu8Data, uint16_t u16Len);
// eStatusDef BspSpiWriteBufferDma(const uint8_t *pu8Data, uint16_t u16Len);
// uint8_t BspSpiIsIdle(void);
// uint8_t BspSpiHasError(void);
// void       BspLcdBusInit(void);
void BspSpiRst(uint8_t u8Level);
void BspSpiDc(uint8_t u8Level);
void BspSpiCs(uint8_t u8Level);
void BspSpiSendByte(uint8_t u8Data);
// eStatusDef BspLcdBusWrite(const uint8_t *pu8Buf, uint16_t u16Len);
// eStatusDef BspLcdBusWriteDma(const uint8_t *pu8Buf, uint16_t u16Len);
// uint8_t    BspLcdBusIsIdle(void);
// uint8_t    BspLcdBusHasError(void);
#ifdef __cplusplus
}
#endif

#endif /* __BSP_SPI_H__ */
