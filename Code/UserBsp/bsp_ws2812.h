#ifndef __BSP_WS2812_H__
#define __BSP_WS2812_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "user_global.h"

#define Pixel_NUM (4)


#define CODE_0      (3)
#define CODE_1      (7)
#define RESET_LEN    (60)
#define TIM_DMA_CH1_CH   DMA1_Channel5
#define COLOR_BUFFER_LEN (((Pixel_NUM)*(3*8))+RESET_LEN)

#ifdef __cplusplus
}
#endif

#endif /* __BSP_WS2812_H__ */
