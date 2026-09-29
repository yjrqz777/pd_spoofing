# coding: utf-8
"""把 xj.jpg 转成 1bpp 黑白点阵，生成 ST7789V 启动图用的 C 头文件。

流程：缩放 135x240 -> 旋转 90 度 -> 灰度 -> Floyd-Steinberg 抖动 -> 1bpp 打包。
输出：Code/Components/font/img_xj_bw.h

之所以用抖动而不是简单阈值：这张图是动漫插画，直接阈值会把脸部糊成一整块白斑，
抖动能把渐变转成疏密网点，远看仍有层次。
"""

from pathlib import Path

import numpy as np
from PIL import Image, ImageOps

# 横屏 240x135，每行 240 像素正好 30 字节，不需要跨字节补位
WIDTH = 240
HEIGHT = 135
ROW_BYTES = WIDTH // 8

SOURCE = "xj.jpg"
OUTPUT_DIR = Path(__file__).resolve().parents[2] / "Code" / "Components" / "font"
PREVIEW = Path(__file__).resolve().parent / "img_xj_bw_preview.png"

ARRAY_NAME = "gau8ImgXjBw"


def load_bits():
    """读源图，缩放旋转后抖动成 0/1 点阵（1 = 白）。"""
    img = Image.open(SOURCE).convert("RGB")
    img = img.resize((HEIGHT, WIDTH))      # PIL 的尺寸参数是 (宽, 高)，这里先摆成竖屏 135x240
    img = img.rotate(90, expand=True)      # 再旋转成横屏 240x135
    if img.size != (WIDTH, HEIGHT):
        raise SystemExit(f"图像尺寸 {img.size} 与 {WIDTH}x{HEIGHT} 不符")

    gray = ImageOps.grayscale(img)
    bw = gray.convert("1", dither=Image.FLOYDSTEINBERG)
    return (np.asarray(bw) > 0).astype(np.uint8)


def pack_rows(bits):
    """按行把每 8 个像素打包成 1 字节，高位在左。"""
    rows = []
    for y in range(HEIGHT):
        rows.append(np.packbits(bits[y]).tobytes())   # packbits 默认就是高位在前
    return rows


def render_header():
    """拼出头文件：只有尺寸宏和外部声明，数据放在同名 .c 里。"""
    macros = (
        ("IMG_XJ_BW_WIDTH",     f"({WIDTH}u)",     "点阵宽度（像素）"),
        ("IMG_XJ_BW_HEIGHT",    f"({HEIGHT}u)",    "点阵高度（像素）"),
        ("IMG_XJ_BW_ROW_BYTES", f"({ROW_BYTES}u)", "每行字节数 = 宽度 / 8，每字节 8 个像素"),
    )
    macro_block = "\n".join(f"#define {n:<22}{v:<8}/* {c} */" for n, v, c in macros)

    return f"""/**
 * @file    img_xj_bw.h
 * @brief   {WIDTH}x{HEIGHT} 黑白启动图点阵声明（自动生成，请勿手工修改）
 *******************************************************************************
 * @note    由 tools/picture/img2bit.py 生成，重新生成请执行：
 *              python tools/picture/img2bit.py
 *          数据本体在 img_xj_bw.c，占用 {HEIGHT * ROW_BYTES} 字节。
 *          格式：逐行连续存放，每行 {ROW_BYTES} 字节，高位在左，1 = 白、0 = 黑。
 *******************************************************************************
 */

#ifndef __IMG_XJ_BW_H__
#define __IMG_XJ_BW_H__

#include <stdint.h>

{macro_block}

extern const uint8_t {ARRAY_NAME}[IMG_XJ_BW_HEIGHT][IMG_XJ_BW_ROW_BYTES];

#endif /* __IMG_XJ_BW_H__ */
"""


def render_source(rows):
    """拼出数据源文件。"""
    lines = []
    for y, row in enumerate(rows):
        data = ", ".join(f"0x{b:02X}" for b in row)
        lines.append(f"    {{ {data} }},   /* 第 {y:3d} 行 */")
    body = "\n".join(lines)

    return f"""/**
 * @file    img_xj_bw.c
 * @brief   {WIDTH}x{HEIGHT} 黑白启动图点阵数据（自动生成，请勿手工修改）
 *******************************************************************************
 * @note    由 tools/picture/img2bit.py 生成，重新生成请执行：
 *              python tools/picture/img2bit.py
 *          源图 {SOURCE} -> 缩放 135x240 -> 旋转 90 度 -> 灰度
 *          -> Floyd-Steinberg 抖动 -> 1bpp 打包。
 *          用抖动而非阈值：插画直接阈值会把脸部糊成一整块白斑，抖动能保留层次。
 *******************************************************************************
 */

#include "img_xj_bw.h"

const uint8_t {ARRAY_NAME}[IMG_XJ_BW_HEIGHT][IMG_XJ_BW_ROW_BYTES] = {{
{body}
}};
"""


def main():
    bits = load_bits()
    rows = pack_rows(bits)

    out_h = OUTPUT_DIR / "img_xj_bw.h"
    out_c = OUTPUT_DIR / "img_xj_bw.c"
    out_h.write_text(render_header(), encoding="utf-8")
    out_c.write_text(render_source(rows), encoding="utf-8")

    # 存一张 1:1 预览，方便肉眼确认抖动效果
    Image.fromarray((bits * 255).astype(np.uint8)).save(PREVIEW)

    white = int(bits.sum())
    print(f"源图      : {SOURCE}")
    print(f"输出      : {out_h}")
    print(f"            {out_c}")
    print(f"尺寸      : {WIDTH}x{HEIGHT}，每行 {ROW_BYTES} 字节")
    print(f"点阵大小  : {HEIGHT * ROW_BYTES} 字节（原始 RGB565 为 {WIDTH * HEIGHT * 2} 字节）")
    print(f"白色像素  : {white}/{WIDTH * HEIGHT}（{white * 100.0 / (WIDTH * HEIGHT):.1f}%）")
    print(f"预览      : {PREVIEW}")


if __name__ == "__main__":
    main()
