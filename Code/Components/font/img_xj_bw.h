/**
 * @file    img_xj_bw.h
 * @brief   240x135 黑白启动图点阵声明（自动生成，请勿手工修改）
 *******************************************************************************
 * @note    由 tools/picture/img2bit.py 生成，重新生成请执行：
 *              python tools/picture/img2bit.py
 *          数据本体在 img_xj_bw.c，占用 4050 字节。
 *          格式：逐行连续存放，每行 30 字节，高位在左，1 = 白、0 = 黑。
 *******************************************************************************
 */

#ifndef __IMG_XJ_BW_H__
#define __IMG_XJ_BW_H__

#include <stdint.h>

#define IMG_XJ_BW_WIDTH       (240u)  /* 点阵宽度（像素） */
#define IMG_XJ_BW_HEIGHT      (135u)  /* 点阵高度（像素） */
#define IMG_XJ_BW_ROW_BYTES   (30u)   /* 每行字节数 = 宽度 / 8，每字节 8 个像素 */

extern const uint8_t gau8ImgXjBw[IMG_XJ_BW_HEIGHT][IMG_XJ_BW_ROW_BYTES];

#endif /* __IMG_XJ_BW_H__ */
