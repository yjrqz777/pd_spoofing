#ifndef __BSP_WS2812_H__
#define __BSP_WS2812_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "user_global.h"

#define WS2812_PIXEL_NUM      (4u)    /* 级联灯珠数量 */
#define WS2812_BITS_PER_PIXEL (24u)   /* 每颗 24 bit（GRB） */
#define WS2812_RESET_SLOT_NUM (60u)   /* 复位槽：60 x 1.25us = 75us > 50us */
#define WS2812_CODE_0         (3u)    /* 位 0 高电平：3/8MHz = 0.375us */
#define WS2812_CODE_1         (7u)    /* 位 1 高电平：7/8MHz = 0.875us */
#define WS2812_DMA_CH         DMA1_Channel5 /* TIM1_CH1 的 DMA 请求通道 */
#define WS2812_SLOT_NUM       ((WS2812_PIXEL_NUM * WS2812_BITS_PER_PIXEL) + WS2812_RESET_SLOT_NUM)

void       BspWs2812Init(void);
eStatusDef BspWs2812LoadBytes(const uint8_t * pu8Bytes, uint16_t u16Len);
eStatusDef BspWs2812Show(void);
uint8_t    BspWs2812IsIdle(void);





#ifdef __cplusplus
}
#endif

#endif /* __BSP_WS2812_H__ */
