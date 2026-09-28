#!/usr/bin/env python3
"""Inter SemiBold -> 2bpp 点阵字库转换器（CH32X035 240x135 仪表界面专用）。

用法::

    python tools/font_converter/generate_fonts.py

输入:
    tools/font_converter/Inter-SemiBold.ttf   （上游 Inter 发行包里的静态 TTF）

输出:
    Code/Components/font/font.h                  （类型与纯接口声明，只生成一次）
    Code/Components/font/font.c                  （纯接口实现，只生成一次）
    Code/Components/font/font_inter_8.c/.h
    Code/Components/font/font_inter_16.c/.h
    Code/Components/font/font_inter_24.c/.h

点阵格式（与 Code/Components/font/font.h 的类型定义一致）:

    * 每像素 2bpp，取值 0..3（0 = 透明，3 = 前景，1/2 = 1/3 与 2/3 混合）；
    * 每字节打包 4 个像素，高位在前（像素 n 占 bit (6 - 2*(n % 4))）；
    * 每行单独按字节对齐后顺序存放，行与行之间不跨字节拼接；
    * 每个字号独立生成，固件端不做字模缩放。

数字 ``0``-``9`` 的前进宽度在生成阶段统一为最大数字宽度（即"表格数字"），
并把字形墨迹在该宽度内居中，避免数值变化时左右跳动。

本脚本输出字库数据、类型头与纯接口实现，不参与固件编译；重新生成是幂等的。
"""

import argparse
import os
import sys

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:  # pragma: no cover - 环境缺依赖时给出明确提示
    sys.stderr.write("ERROR: 需要 Pillow（pip install Pillow）\n")
    raise

# --------------------------------------------------------------------------- #
#  字符子集
# --------------------------------------------------------------------------- #

#: 24px 数字字库只包含数值格式化用得到的字符
DIGIT_CHARS = "0123456789.-VWAMm"

#: 16px 字库在数字之外还需要 OUTPUT 区域的 ON/OFF 文字，
#: 以及顶部状态栏的 "POWER MONITOR" / "ONLINE" 标题（需要全部用到的字母与空格）
DIGIT_AND_SWITCH_CHARS = " 0123456789.-EFLIMNOPRTW"

#: 8px 标签字库只包含界面实际用到的字符
#: （大写字母、数字、空格、点、短横线、斜杠）
LABEL_CHARS = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-/"

#: (字库名, 像素高度, 字符子集)
FONT_SET = (
    ("font_inter_28", 28, DIGIT_CHARS),
    ("font_inter_24", 24, DIGIT_CHARS),
    ("font_inter_16", 16, DIGIT_AND_SWITCH_CHARS),
    # ("font_inter_8", 8, LABEL_CHARS),
)

#: 需要统一前进宽度的字符（表格数字）
UNIFORM_ADVANCE_CHARS = "0123456789"

#: tFontGlyph 在 32 位平台上的字节数（4+1+1+1+1+1+3 填充+4），
#: 必须与生成出来的 Code/Components/font/font.h 结构体定义一致，否则体积统计会不准。
FONT_GLYPH_BYTES = 16

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEFAULT_TTF = os.path.join(REPO_ROOT, "tools", "font_converter", "Inter-SemiBold.ttf")
DEFAULT_OUT = os.path.join(REPO_ROOT, "Code", "Components", "font")

# --------------------------------------------------------------------------- #
#  栅格化
# --------------------------------------------------------------------------- #


def quantize_alpha(value):
    """把 PIL 的 8bit 覆盖度（0..255）量化成 2bpp 覆盖度（0..3）。

    线性量化：渲染端按 (fg*a + bg*(3-a)) / 3 混合，因此这里不能再做伽马修正，
    否则混合结果会比设计稿偏重或偏轻。
    """
    return (value * 3 + 127) // 255


def rasterize(font, char):
    """把一个字符栅格化成 2bpp 位图。

    :return: (width, height, x_offset, y_offset, advance, packed_rows)
             ``x_offset`` / ``y_offset`` 是相对"笔位置"的偏移，``y_offset`` 为负
             表示字形在基线上方。``packed_rows`` 是每行一个 bytearray 的列表。
    """
    bbox = font.getbbox(char, anchor="ls")
    x0, y0, x1, y1 = bbox
    width = x1 - x0
    height = y1 - y0
    advance = int(round(font.getlength(char)))

    if width <= 0 or height <= 0:
        # 空格等无墨迹字符：只占前进宽度，没有位图
        return 0, 0, 0, 0, advance, []

    image = Image.new("L", (width, height), 0)
    draw = ImageDraw.Draw(image)
    draw.text((-x0, -y0), char, font=font, fill=255, anchor="ls")

    bytes_per_row = (width + 3) // 4
    rows = []
    for y in range(height):
        row = bytearray(bytes_per_row)
        for x in range(width):
            alpha = quantize_alpha(image.getpixel((x, y)))
            if alpha:
                row[x // 4] |= alpha << (6 - 2 * (x % 4))
        rows.append(row)

    return width, height, x0, y0, advance, rows


def build_font(ttf_path, pixel_size, chars):
    """栅格化一个字号的全部字符，返回字形列表。"""
    font = ImageFont.truetype(ttf_path, pixel_size)
    ascent, descent = font.getmetrics()

    glyphs = []
    for char in chars:
        width, height, x_offset, y_offset, advance, rows = rasterize(font, char)
        glyphs.append(
            {
                "char": char,
                "width": width,
                "height": height,
                "x_offset": x_offset,
                "y_offset": y_offset,
                "advance": advance,
                "rows": rows,
            }
        )

    # 数字统一前进宽度：取所有数字的最大自然宽度，并把墨迹在该宽度内居中，
    # 这样 "12.08" 与 "9.99" 这类字符串的总宽度只由字符数决定。
    digit_advances = [g["advance"] for g in glyphs if g["char"] in UNIFORM_ADVANCE_CHARS]
    if digit_advances:
        uniform = max(digit_advances)
        for g in glyphs:
            if g["char"] in UNIFORM_ADVANCE_CHARS and g["width"] > 0:
                g["advance"] = uniform
                g["x_offset"] = (uniform - g["width"]) // 2

    line_height = ascent + descent
    return glyphs, ascent, line_height


# --------------------------------------------------------------------------- #
#  C 代码生成
# --------------------------------------------------------------------------- #

FONT_HDR_TEMPLATE = """/**
 * @file    font.h
 * @brief   2bpp 点阵字库的类型与纯接口（自动生成，请勿手工修改）
 *******************************************************************************
 * @note    由 tools/font_converter/generate_fonts.py 生成。
 *          本模块属于 Components：只做字形查找、宽度测量与 2bpp 覆盖度混合，
 *          不依赖任何板级、器件或产品规则。往屏上画的动作在 Device 层，
 *          由它逐行调用 FontRenderRow() 取像素，再走自己的发送通道。
 *
 *          坐标约定：
 *            - 字形落点 = (笔位置 + x_offset, 基线 + y_offset)；
 *            - y_offset 为负表示墨迹位于基线上方。
 *
 *          点阵格式：2bpp（每像素 0..3），每字节打包 4 像素，高位在前，
 *          每行按字节对齐，行与行之间不跨字节拼接。
 *
 *          使用示例（只到"取到像素"为止，送屏那一半在 Device 层）：
 *
 *              #include "font_inter_24.h"      // 里面已经带了 font.h
 *
 *              const char       *pcText  = "12.08";
 *              const tFontGlyph *ptGlyph = 0;
 *              uint16_t          au16Line[32];        // 一行像素缓冲
 *              uint16_t          u16Row;
 *              int16_t           i16PenX = 8;         // 笔位置
 *              int16_t           i16BaseY = 40;       // 基线
 *
 *              // 1) 先量这条字符串占多宽（用来放布局、判左右对齐）
 *              u16TextW = FontMeasureText(&gtFontInter24, pcText);
 *
 *              // 2) 逐字符取字形，逐行取像素
 *              while (*pcText != '\\0')
 *              {
 *                  ptGlyph = FontFindGlyph(&gtFontInter24, (uint8_t)*pcText);
 *                  if (ptGlyph != 0)
 *                  {
 *                      for (u16Row = 0u; u16Row < ptGlyph->height; u16Row++)
 *                      {
 *                          FontRenderRow(&gtFontInter24, ptGlyph, u16Row,
 *                                        u16Fg, u16Bg, au16Line, 32u);
 *
 *                          // 3) 把 au16Line 交给 Device 层的矩形发送：
 *                          //    x = i16PenX + ptGlyph->x_offset
 *                          //    y = i16BaseY + ptGlyph->y_offset + u16Row
 *                          //    宽 = ptGlyph->width，高 = 1 行
 *                      }
 *                  }
 *                  i16PenX += ptGlyph->advance;   // 笔位置前移
 *                  pcText++;
 *              }
 *******************************************************************************
 */

#ifndef __FONT_H__
#define __FONT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/** @brief 单个字形的描述（由字库生成脚本填充） */
typedef struct
{
    uint32_t bitmap_offset;   /**< 在字库位图数组中的字节偏移 */
    uint8_t  width;           /**< 字形位图宽（像素），0 表示无墨迹（如空格） */
    uint8_t  height;          /**< 字形位图高（像素），0 表示无墨迹 */
    int8_t   x_offset;        /**< 相对笔位置的横向偏移（像素） */
    int8_t   y_offset;        /**< 相对基线的纵向偏移（像素），负值表示在上方 */
    uint8_t  advance;         /**< 前进宽度（像素） */
    uint32_t codepoint;       /**< 字符码点（ASCII） */
} tFontGlyph;

/** @brief 一套字库（位图 + 字形表 + 行高信息） */
typedef struct
{
    const uint8_t    *bitmap;      /**< 位图数据（2bpp，逐行按字节对齐） */
    const tFontGlyph *glyphs;      /**< 字形描述表 */
    uint16_t          glyph_count; /**< 字形数量 */
    uint8_t           line_height; /**< 行高（ascent + descent） */
    uint8_t           baseline;    /**< 顶边到基线的距离（ascent） */
} tFont;

/**
 * @brief  查询一个字符的字形
 * @param[in] ptFont  字库
 * @param[in] u8Char  字符
 * @return 字形描述指针；字符不在字库中时返回 0
 */
const tFontGlyph *FontFindGlyph(const tFont *ptFont, uint8_t u8Char);

/**
 * @brief  测量一段文本的前进宽度
 * @param[in] ptFont  字库
 * @param[in] pcText  以 '\\0' 结尾的字符串
 * @return 文本宽度（像素）；未知字符按 0 计算
 */
uint16_t FontMeasureText(const tFont *ptFont, const char *pcText);

/**
 * @brief  RGB565 前景/背景按 2bpp 覆盖度线性混合
 * @param[in] u16Fg    前景色
 * @param[in] u16Bg    背景色
 * @param[in] u8Alpha  覆盖度 0..3
 * @return 混合后的 RGB565 颜色
 */
uint16_t FontBlend565(uint16_t u16Fg, uint16_t u16Bg, uint8_t u8Alpha);

/**
 * @brief  把某个字形的第 u16Row 行渲染成 RGB565 像素
 * @param[in]  ptFont       字库
 * @param[in]  ptGlyph      字形
 * @param[in]  u16Row       行号，0 = 字形顶边
 * @param[in]  u16Fg        前景色
 * @param[in]  u16Bg        背景色
 * @param[out] pu16Line     输出像素缓冲
 * @param[in]  u16Capacity  缓冲能容纳的像素数
 * @return 实际写入的像素数；行号越界或缓冲不够时为 0
 * @note   只填像素，不设窗口、不做字节序转换，那些是发送端的事。
 */
uint16_t FontRenderRow(const tFont *ptFont, const tFontGlyph *ptGlyph, uint16_t u16Row,
                       uint16_t u16Fg, uint16_t u16Bg, uint16_t *pu16Line, uint16_t u16Capacity);

#ifdef __cplusplus
}
#endif

#endif /* __FONT_H__ */
"""

FONT_SRC_TEMPLATE = """/**
 * @file    font.c
 * @brief   2bpp 点阵字库的纯接口实现（自动生成，请勿手工修改）
 *******************************************************************************
 * @note    由 tools/font_converter/generate_fonts.py 生成。
 *          本文件不依赖任何板级、器件或产品规则：只读字库数据，
 *          只写调用方给的像素缓冲。往屏上画的动作在 Device 层。
 *******************************************************************************
 */

#include "font.h"

const tFontGlyph *FontFindGlyph(const tFont *ptFont, uint8_t u8Char)
{
    uint16_t u16Index;

    if (ptFont == 0)
    {
        return 0;
    }

    for (u16Index = 0u; u16Index < ptFont->glyph_count; u16Index++)
    {
        if (ptFont->glyphs[u16Index].codepoint == (uint32_t)u8Char)
        {
            return &ptFont->glyphs[u16Index];
        }
    }

    return 0;
}

uint16_t FontMeasureText(const tFont *ptFont, const char *pcText)
{
    const tFontGlyph *ptGlyph;
    uint16_t u16Width = 0u;

    if ((ptFont == 0) || (pcText == 0))
    {
        return 0u;
    }

    while (*pcText != '\\0')
    {
        ptGlyph = FontFindGlyph(ptFont, (uint8_t)*pcText);
        if (ptGlyph != 0)
        {
            u16Width = (uint16_t)(u16Width + ptGlyph->advance);
        }
        pcText++;
    }

    return u16Width;
}

uint16_t FontBlend565(uint16_t u16Fg, uint16_t u16Bg, uint8_t u8Alpha)
{
    uint32_t u32R;
    uint32_t u32G;
    uint32_t u32B;

    if (u8Alpha >= 3u)
    {
        return u16Fg;
    }
    if (u8Alpha == 0u)
    {
        return u16Bg;
    }

    /* RGB565：R[15:11] G[10:5] B[4:0]，按 alpha/3 在前景与背景之间线性混合 */
    u32R = ((((uint32_t)(u16Fg >> 11) & 0x1Fu) * u8Alpha +
             ((uint32_t)(u16Bg >> 11) & 0x1Fu) * (3u - u8Alpha)) / 3u) & 0x1Fu;
    u32G = ((((uint32_t)(u16Fg >> 5) & 0x3Fu) * u8Alpha +
             ((uint32_t)(u16Bg >> 5) & 0x3Fu) * (3u - u8Alpha)) / 3u) & 0x3Fu;
    u32B = ((((uint32_t)u16Fg & 0x1Fu) * u8Alpha +
             ((uint32_t)u16Bg & 0x1Fu) * (3u - u8Alpha)) / 3u) & 0x1Fu;

    return (uint16_t)((u32R << 11) | (u32G << 5) | u32B);
}

uint16_t FontRenderRow(const tFont *ptFont, const tFontGlyph *ptGlyph, uint16_t u16Row,
                       uint16_t u16Fg, uint16_t u16Bg, uint16_t *pu16Line, uint16_t u16Capacity)
{
    const uint8_t *pu8Row;
    uint16_t u16Col;
    uint8_t  u8Alpha;

    if ((ptFont == 0) || (ptGlyph == 0) || (pu16Line == 0))
    {
        return 0u;
    }
    if ((ptGlyph->width == 0u) || (u16Row >= ptGlyph->height))
    {
        return 0u;
    }
    if (u16Capacity < ptGlyph->width)
    {
        return 0u;
    }

    /* 位图每行按字节对齐，每字节 4 个像素，高位在前 */
    pu8Row = &ptFont->bitmap[ptGlyph->bitmap_offset +
                            (uint32_t)u16Row * ((ptGlyph->width + 3u) / 4u)];

    for (u16Col = 0u; u16Col < ptGlyph->width; u16Col++)
    {
        u8Alpha = (uint8_t)((pu8Row[u16Col / 4u] >> (6u - 2u * (u16Col % 4u))) & 0x03u);
        pu16Line[u16Col] = FontBlend565(u16Fg, u16Bg, u8Alpha);
    }

    return (uint16_t)ptGlyph->width;
}
"""

HEADER_TEMPLATE = """/**
 * @file    {name}.h
 * @brief   {size}px Inter SemiBold 精简点阵字库（自动生成，请勿手工修改）
 *******************************************************************************
 * @note    由 tools/font_converter/generate_fonts.py 生成。
 *          字体来源：Inter SemiBold, https://github.com/rsms/inter
 *          许可证见 tools/font_converter/LICENSE.txt（SIL Open Font License 1.1）。
 *          字符子集：{chars}
 *          字形墨迹：基线以上 {above} 行、以下 {below} 行，一行最高占 {ink_height} 行。
 *                    定格布局：顶边 = 基线 - {above}；行间距用 {line_height}（line_height）。
 *          字体格式：2bpp（每像素 0..3，每字节打包 4 像素，高位在前，逐行对齐）。
 *******************************************************************************
 */

#ifndef __{guard}_H__
#define __{guard}_H__

#ifdef __cplusplus
extern "C" {{
#endif

#include "font.h"

extern const tFont gtFontInter{size};

#ifdef __cplusplus
}}
#endif

#endif /* __{guard}_H__ */
"""

SOURCE_TEMPLATE = """/**
 * @file    {name}.c
 * @brief   {size}px Inter SemiBold 精简点阵字库数据（自动生成，请勿手工修改）
 *******************************************************************************
 * @note    由 tools/font_converter/generate_fonts.py 生成，重新生成请执行：
 *              python tools/font_converter/generate_fonts.py
 *          位图共 {bitmap_bytes} 字节，字形描述表共 {glyph_table_bytes} 字节。
 *******************************************************************************
 */

#include "{name}.h"

/** @brief {size}px 字形位图（2bpp，每行按字节对齐） */
static const uint8_t saBitmap{size}[{bitmap_len}] = {{
{bitmap_body}}};

/** @brief {size}px 字形描述表 */
static const tFontGlyph saGlyph{size}[{count}] = {{
{glyph_body}}};

/** @brief {size}px Inter SemiBold 字库（墨迹 {ink_height} 行：基线以上 {above}、以下 {below}） */
const tFont gtFontInter{size} = {{
    saBitmap{size},
    saGlyph{size},
    {count}u,
    {line_height}u,
    {baseline}u
}};
"""


def emit_font_api(out_dir):
    """写出公共类型头与纯接口实现，跟字库放同一个目录，各写一次。"""
    files = (
        ("font.h", FONT_HDR_TEMPLATE, "类型与纯接口声明"),
        ("font.c", FONT_SRC_TEMPLATE, "纯接口实现"),
    )
    for fname, text, desc in files:
        with open(os.path.join(out_dir, fname), "w", encoding="utf-8", newline="\n") as fp:
            fp.write(text)
        print("  %-14s %5d B  %s" % (fname, len(text), desc))


def check_ranges(name, glyphs):
    """字形字段要装进 tFontGlyph 的窄类型，越界就报错，不要悄悄截断。"""
    for g in glyphs:
        if not (0 <= g["width"] <= 255) or not (0 <= g["height"] <= 255):
            raise SystemExit(
                "ERROR: %s 字形 '%s' 宽高 %dx%d 超出 uint8_t" % (name, g["char"], g["width"], g["height"])
            )
        if not (0 <= g["advance"] <= 255):
            raise SystemExit(
                "ERROR: %s 字形 '%s' 前进宽度 %d 超出 uint8_t" % (name, g["char"], g["advance"])
            )
        if not (-128 <= g["x_offset"] <= 127) or not (-128 <= g["y_offset"] <= 127):
            raise SystemExit(
                "ERROR: %s 字形 '%s' 偏移 (%d, %d) 超出 int8_t"
                % (name, g["char"], g["x_offset"], g["y_offset"])
            )


def emit_font(out_dir, name, pixel_size, chars, glyphs, ascent, line_height):
    """写出一个字号的字库 .c/.h，返回 (位图字节数, 字形表字节数)。"""
    guard = name.upper()
    size = pixel_size

    check_ranges(name, glyphs)

    # 1) 位图：逐字形顺序拼接，记录每个字形的起始偏移
    bitmap = bytearray()
    glyph_table = []
    for g in glyphs:
        offset = len(bitmap)
        for row in g["rows"]:
            bitmap.extend(row)
        glyph_table.append((offset, g))

    # 2) 位图数组文本，每行 12 字节
    if bitmap:
        lines = []
        for i in range(0, len(bitmap), 12):
            chunk = bitmap[i : i + 12]
            lines.append("    " + " ".join("0x%02X," % b for b in chunk))
        bitmap_body = "\n".join(lines) + "\n"
        bitmap_len = len(bitmap)
    else:
        # 空数组在 ISO C 里不合法，占位一个字节；初始化列表同样不能为空
        bitmap = bytearray(b"\x00")
        bitmap_body = "    0x00,\n"
        bitmap_len = 1

    # 3) 字形描述表文本
    glyph_lines = []
    for offset, g in glyph_table:
        glyph_lines.append(
            "    {{ {offset:6d}u, {width:3d}u, {height:3d}u, "
            "{x_off:4d}, {y_off:4d}, {adv:3d}u, {code:#010x}u }},  /* '{char}' */".format(
                offset=offset,
                width=g["width"],
                height=g["height"],
                x_off=g["x_offset"],
                y_off=g["y_offset"],
                adv=g["advance"],
                code=ord(g["char"]),
                char=("space" if g["char"] == " " else g["char"]),
            )
        )
    glyph_body = "\n".join(glyph_lines) + "\n"

    # 字形墨迹相对基线的上下范围：定格布局直接看这两个数
    above = max([-g["y_offset"] for g in glyphs if g["height"] > 0], default=0)
    below = max([g["y_offset"] + g["height"] for g in glyphs], default=0)

    values = {
        "name": name,
        "guard": guard,
        "size": size,
        "chars": "".join(chars),
        "count": len(glyphs),
        "bitmap_len": bitmap_len,
        "bitmap_body": bitmap_body,
        "glyph_body": glyph_body,
        "line_height": line_height,
        "baseline": ascent,
        "above": above,
        "below": below,
        "ink_height": above + below,
        "bitmap_bytes": len(bitmap),
        "glyph_table_bytes": len(glyphs) * FONT_GLYPH_BYTES,
    }

    with open(os.path.join(out_dir, name + ".h"), "w", encoding="utf-8", newline="\n") as fp:
        fp.write(HEADER_TEMPLATE.format(**values))
    with open(os.path.join(out_dir, name + ".c"), "w", encoding="utf-8", newline="\n") as fp:
        fp.write(SOURCE_TEMPLATE.format(**values))

    max_glyph_w = max((g["width"] for g in glyphs), default=0)
    max_glyph_h = max((g["height"] for g in glyphs), default=0)
    print(
        "  %-14s %2dpx  字符 %2d  位图 %4d B  字形表 %4d B  行高 %2d 基线 %2d  "
        "最大字形 %dx%d  占高 上%d下%d"
        % (
            name,
            size,
            len(glyphs),
            len(bitmap),
            len(glyphs) * FONT_GLYPH_BYTES,
            line_height,
            ascent,
            max_glyph_w,
            max_glyph_h,
            above,
            below,
        )
    )
    return len(bitmap), len(glyphs) * FONT_GLYPH_BYTES


def main():
    parser = argparse.ArgumentParser(description="Inter SemiBold -> 2bpp 点阵字库")
    parser.add_argument("--ttf", default=DEFAULT_TTF, help="Inter-SemiBold.ttf 路径")
    parser.add_argument("--out-dir", default=DEFAULT_OUT, help="字库输出目录")
    args = parser.parse_args()

    if not os.path.isfile(args.ttf):
        sys.stderr.write("ERROR: 找不到字体文件 %s\n" % args.ttf)
        sys.stderr.write("       请从 https://github.com/rsms/inter/releases 下载发行包，\n")
        sys.stderr.write("       解压出 extras/ttf/Inter-SemiBold.ttf 放到 tools/font_converter/。\n")
        return 1

    os.makedirs(args.out_dir, exist_ok=True)

    print("Inter SemiBold 2bpp 字库生成")
    print("  源字体 : %s" % args.ttf)
    print("  输出   : %s" % args.out_dir)

    emit_font_api(args.out_dir)

    total_bitmap = 0
    total_glyphs = 0
    for name, pixel_size, chars in FONT_SET:
        glyphs, ascent, line_height = build_font(args.ttf, pixel_size, chars)
        bitmap_bytes, glyph_bytes = emit_font(
            args.out_dir, name, pixel_size, chars, glyphs, ascent, line_height
        )
        total_bitmap += bitmap_bytes
        total_glyphs += glyph_bytes

    print("  合计   : 位图 %d B + 字形表 %d B = %d B" % (total_bitmap, total_glyphs, total_bitmap + total_glyphs))
    return 0


if __name__ == "__main__":
    sys.exit(main())
