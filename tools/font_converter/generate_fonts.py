#!/usr/bin/env python3
"""Inter SemiBold -> 2bpp 点阵字库转换器（CH32X035 240x135 仪表界面专用）。

用法::

    python tools/font_converter/generate_fonts.py

输入:
    tools/font_converter/Inter-SemiBold.ttf   （上游 Inter 发行包里的静态 TTF）

输出:
    Code/UserApp/ui/fonts/font_inter_8.c/.h
    Code/UserApp/ui/fonts/font_inter_16.c/.h
    Code/UserApp/ui/fonts/font_inter_24.c/.h

点阵格式（与 Code/UserApp/ui/ui_font.h 的渲染器约定一致）:

    * 每像素 2bpp，取值 0..3（0 = 透明，3 = 前景，1/2 = 1/3 与 2/3 混合）；
    * 每字节打包 4 个像素，高位在前（像素 n 占 bit (6 - 2*(n % 4))）；
    * 每行单独按字节对齐后顺序存放，行与行之间不跨字节拼接；
    * 每个字号独立生成，固件端不做字模缩放。

数字 ``0``-``9`` 的前进宽度在生成阶段统一为最大数字宽度（即"表格数字"），
并把字形墨迹在该宽度内居中，避免数值变化时左右跳动。

本脚本只输出字库数据，不参与固件编译；重新生成是幂等的。
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
DIGIT_CHARS = "0123456789.-"

#: 16px 字库在数字之外还需要 OUTPUT 区域的 ON/OFF 文字，
#: 以及顶部状态栏的 "POWER MONITOR" / "ONLINE" 标题（需要全部用到的字母与空格）
DIGIT_AND_SWITCH_CHARS = " 0123456789.-EFLIMNOPRTW"

#: 8px 标签字库只包含界面实际用到的字符
#: （大写字母、数字、空格、点、短横线、斜杠）
LABEL_CHARS = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-/"

#: (字库名, 像素高度, 字符子集)
FONT_SET = (
    ("font_inter_24", 24, DIGIT_CHARS),
    ("font_inter_16", 16, DIGIT_AND_SWITCH_CHARS),
    ("font_inter_8", 8, LABEL_CHARS),
)

#: 需要统一前进宽度的字符（表格数字）
UNIFORM_ADVANCE_CHARS = "0123456789"

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEFAULT_TTF = os.path.join(REPO_ROOT, "tools", "font_converter", "Inter-SemiBold.ttf")
DEFAULT_OUT = os.path.join(REPO_ROOT, "Code", "UserApp", "ui", "fonts")

# --------------------------------------------------------------------------- #
#  栅格化
# --------------------------------------------------------------------------- #


def quantize_alpha(value):
    """把 PIL 的 8bit 覆盖度（0..255）量化成 2bpp 覆盖度（0..3）。

    线性量化：渲染端按 (fg*a + bg*(3-a)) / 3 混合，因此这里不能再做伽马修正，
    否则混合结果会比设计稿偏重或偏轻。
    """
    return min(3, (value * 3 + 127) // 255)


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

HEADER_TEMPLATE = """/**
 * @file    {name}.h
 * @brief   {size}px Inter SemiBold 精简点阵字库（自动生成，请勿手工修改）
 *******************************************************************************
 * @note    由 tools/font_converter/generate_fonts.py 生成。
 *          字体来源：Inter SemiBold, https://github.com/rsms/inter
 *          许可证见 tools/font_converter/LICENSE.txt（SIL Open Font License 1.1）。
 *          字符子集：{chars}
 *          字体格式：2bpp（每像素 0..3，每字节打包 4 像素，高位在前，逐行对齐）。
 *******************************************************************************
 */

#ifndef __{guard}_H__
#define __{guard}_H__

#ifdef __cplusplus
extern "C" {{
#endif

#include "ui_font.h"

/** @brief {size}px Inter SemiBold 字库（字符数 {count}） */
extern const UiFont g_tUiFontInter{size};

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
static const uint8_t s_au8Bitmap{size}[{bitmap_len}] = {{
{bitmap_body}}};

/** @brief {size}px 字形描述表 */
static const UiGlyph s_atGlyph{size}[{count}] = {{
{glyph_body}}};

/** @brief {size}px Inter SemiBold 字库 */
const UiFont g_tUiFontInter{size} = {{
    s_au8Bitmap{size},
    s_atGlyph{size},
    {count}u,
    {line_height}u,
    {baseline}u
}};
"""


def c_escape(char):
    """把字符写成 C 字符字面量。"""
    if char == "\\":
        return "'\\\\'"
    if char == "'":
        return "'\\''"
    return "'%s'" % char


def emit_font(out_dir, name, pixel_size, chars, glyphs, ascent, line_height):
    """写出一个字号的字库 .c/.h，返回 (位图字节数, 字形表字节数)。"""
    guard = name.upper()
    size = pixel_size

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
        bitmap_body = ""
        bitmap_len = 1  # 空数组在 ISO C 里不合法，占位一个字节
        bitmap = bytearray(b"\x00")

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
        "bitmap_bytes": len(bitmap),
        "glyph_table_bytes": len(glyphs) * 16,  # UiGlyph 在 32 位平台占 16 字节
    }

    with open(os.path.join(out_dir, name + ".h"), "w", encoding="utf-8", newline="\n") as fp:
        fp.write(HEADER_TEMPLATE.format(**values))
    with open(os.path.join(out_dir, name + ".c"), "w", encoding="utf-8", newline="\n") as fp:
        fp.write(SOURCE_TEMPLATE.format(**values))

    max_glyph_w = max((g["width"] for g in glyphs), default=0)
    max_glyph_h = max((g["height"] for g in glyphs), default=0)
    print(
        "  %-14s %2dpx  字符 %2d  位图 %4d B  字形表 %4d B  行高 %2d 基线 %2d  "
        "最大字形 %dx%d"
        % (
            name,
            size,
            len(glyphs),
            len(bitmap),
            len(glyphs) * 16,
            line_height,
            ascent,
            max_glyph_w,
            max_glyph_h,
        )
    )
    return len(bitmap), len(glyphs) * 16


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
