/**
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
 *              while (*pcText != '\0')
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
 * @param[in] pcText  以 '\0' 结尾的字符串
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
