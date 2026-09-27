/**
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

    while (*pcText != '\0')
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
