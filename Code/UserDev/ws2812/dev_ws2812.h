#ifndef __DEV_WS2812_H__
#define __DEV_WS2812_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "user_global.h"

#define DEV_WS2812_FRAME_LEN   ((WS2812_PIXEL_NUM) * 3u)   /* GRB 字节数：4 颗 -> 12 */

void       DevWs2812Init(void);
eStatusDef DevWs2812SetPixel(uint16_t u16Index, uint8_t u8Red, uint8_t u8Green, uint8_t u8Blue);
eStatusDef DevWs2812Fill(uint8_t u8Red, uint8_t u8Green, uint8_t u8Blue);
eStatusDef DevWs2812Flush(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_WS2812_H__ */
