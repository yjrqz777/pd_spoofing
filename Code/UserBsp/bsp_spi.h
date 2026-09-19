/**
 * @file bsp_spi.h
 * @brief Declares the LCD SPI1 and transmit-DMA interface.
 */

#ifndef __BSP_SPI_H__
#define __BSP_SPI_H__

#ifdef __cplusplus
extern "C" {
#endif



#define BSP_SPI_TIMEOUT_COUNT (0x100000u) /* Peripheral polling timeout. */

void BspSpiInit(void);
void BspSpiWriteByte(uint8_t u8Data);
void BspSpiWriteBufferBlocking(const uint8_t *pu8Data, uint16_t u16Len);
eStatusDef BspSpiWriteBufferDma(const uint8_t *pu8Data, uint16_t u16Len);
uint8_t BspSpiIsIdle(void);
uint8_t BspSpiHasError(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_SPI_H__ */
