/**
 * @file    color.h
 * @brief   与设备无关的颜色工具接口。
 *
 * @note    本模块属于 Components：不依赖任何板级、器件或产品规则，
 *          只负责把 HSV 描述换算成 RGB 分量，供上层绘制或点灯使用。
 */

#ifndef __COLOR_H__
#define __COLOR_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define COLOR_HUE_MAX   (359u)   /* 色相上限（度）：0=红，120=绿，240=蓝 */
#define COLOR_SV_MAX    (255u)   /* 饱和度与明度的上限 */

void ColorHsvToRgb(uint16_t u16H, uint8_t u8S, uint8_t u8V,
                   uint8_t * pu8R, uint8_t * pu8G, uint8_t * pu8B);

#ifdef __cplusplus
}
#endif

#endif /* __COLOR_H__ */
