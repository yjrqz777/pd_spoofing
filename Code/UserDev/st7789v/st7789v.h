/**
 * @file    st7789v.h
 * @brief   ST7789V LCD 驱动头文件 — SPI 接口 240×135 彩色液晶屏
 *******************************************************************************
 * @note    支持 4 种显示方向（横屏/竖屏），16-bit RGB565 色彩
 *          提供点、线、矩形、圆、汉字、字符、字符串、数字、图片等绘图接口
 *
 *          硬件接口（已移植到 CH32X035 + WCH 标准外设库）：
 *            - SPI1 主机 Mode 2：SCK = PA5，MOSI(SDA) = PA7
 *            - GPIO：CS = PA3，DC = PA2，RES = PA1
 *            - SPI1_TX 使用 DMA1 通道 3 异步发送像素缓冲区
 *          引脚定义来自 Code/main.h，与原理图 NETLIST 一致。
 *******************************************************************************
 */

#ifndef __ST7789V_H__
#define __ST7789V_H__
#include "main.h"
#include "bsp_spi.h"

#include <stdio.h>

/* LCD 硬件控制引脚
 * 注意：本板 N114-2413THBIG01-H13 的背光 LEDK 硬件直接接地（常亮），无背光控制脚 */
#define LCD_RST(x)  do { if (x) { GPIO_SetBits(LCD_RES_PORT, LCD_RES_PIN); } else { GPIO_ResetBits(LCD_RES_PORT, LCD_RES_PIN); } } while (0)
#define LCD_DC(x)   do { if (x) { GPIO_SetBits(LCD_DC_PORT,  LCD_DC_PIN);  } else { GPIO_ResetBits(LCD_DC_PORT,  LCD_DC_PIN);  } } while (0)
#define LCD_CS(x)   do { if (x) { GPIO_SetBits(LCD_CS_PORT,  LCD_CS_PIN);  } else { GPIO_ResetBits(LCD_CS_PORT,  LCD_CS_PIN);  } } while (0)

/* SPI 数据/命令标识 */
#define CMD  0
#define DATA 1

/* 显示偏移校正 */
#define WIDTH_OFFSET  0
#define HIGH_OFFSET   20

/** @brief 显示方向：0/1=竖屏(135×240)，2/3=横屏(240×135) */
#define USE_HORIZONTAL 3

#if USE_HORIZONTAL==0||USE_HORIZONTAL==1
#define LCD_W  135
#define LCD_H  240
#else
#define LCD_W  240
#define LCD_H  135
#endif

#define LCD_BUFF_SIZE  (LCD_W * LCD_H)

/* 颜色定义（RGB565 格式） */
#define WHITE          (uint16_t)0xFFFF
#define BLACK          (uint16_t)0x0000
#define BLUE           (uint16_t)0x001F
#define BRED           (uint16_t)0XF81F
#define GRED           (uint16_t)0XFFE0
#define GBLUE          (uint16_t)0X07FF
#define RED            (uint16_t)0xF800
#define MAGENTA        (uint16_t)0xF81F
#define GREEN          (uint16_t)0x07E0
#define CYAN           (uint16_t)0x7FFF
#define YELLOW         (uint16_t)0xFFE0
#define BROWN          (uint16_t)0XBC40
#define BRRED          (uint16_t)0XFC07
#define GRAY           (uint16_t)0X8430
#define DARKBLUE       (uint16_t)0X01CF
#define LIGHTBLUE      (uint16_t)0X7D7C
#define GRAYBLUE       (uint16_t)0X5458
#define LIGHTGREEN     (uint16_t)0X841F
#define LGRAY          (uint16_t)0XC618
#define LGRAYBLUE      (uint16_t)0XA651
#define LBBLUE         (uint16_t)0X2B12

/* 外部函数声明 */
extern void st7789v_init(void);
extern void LCD_color_point(uint16_t x1, uint16_t y1, uint16_t color);
uint8_t LCD_IsTransferBusy(void);

/* 绘图接口 */
void LCD_Fill(uint16_t xsta, uint16_t ysta, uint16_t xend, uint16_t yend, uint16_t color);
void LCD_DrawPoint(uint16_t x, uint16_t y, uint16_t color);
void LCD_DrawLine(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);
void LCD_DrawRectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);
void Draw_Circle(uint16_t x0, uint16_t y0, uint8_t r, uint16_t color);

/* 汉字显示（12×12 / 16×16 / 24×24 / 32×32） */
void LCD_ShowChinese(uint16_t x, uint16_t y, uint8_t *s, uint16_t fc, uint16_t bc, uint8_t sizey, uint8_t mode);
void LCD_ShowChinese12x12(uint16_t x, uint16_t y, uint8_t *s, uint16_t fc, uint16_t bc, uint8_t sizey, uint8_t mode);
void LCD_ShowChinese16x16(uint16_t x, uint16_t y, uint8_t *s, uint16_t fc, uint16_t bc, uint8_t sizey, uint8_t mode);
void LCD_ShowChinese24x24(uint16_t x, uint16_t y, uint8_t *s, uint16_t fc, uint16_t bc, uint8_t sizey, uint8_t mode);
void LCD_ShowChinese32x32(uint16_t x, uint16_t y, uint8_t *s, uint16_t fc, uint16_t bc, uint8_t sizey, uint8_t mode);

/* ASCII 字符 / 字符串 */
void LCD_ShowChar(uint16_t x, uint16_t y, uint8_t num, uint16_t fc, uint16_t bc, uint8_t sizey, uint8_t mode);
void LCD_ShowString(uint16_t x, uint16_t y, const uint8_t *p, uint16_t fc, uint16_t bc, uint8_t sizey, uint8_t mode);

/* 数字显示 */
uint32_t mypow(uint8_t m, uint8_t n);
void LCD_ShowIntNum(uint16_t x, uint16_t y, uint16_t num, uint8_t len, uint16_t fc, uint16_t bc, uint8_t sizey);
eStatusDef LCD_ShowIntNumAsync(uint16_t x, uint16_t y, uint32_t num, uint8_t len,
                               uint16_t fc, uint16_t bc, uint8_t sizey);
void LCD_ShowFloatNum1(uint16_t x, uint16_t y, float num, uint8_t len, uint16_t fc, uint16_t bc, uint8_t sizey);
eStatusDef LCD_ShowFloatNumAsync(uint16_t x, uint16_t y, float fValue,
                                 uint8_t u8Length, uint8_t u8Decimals,
                                 uint16_t fc, uint16_t bc, uint8_t sizey);

/* ==========================================================================
 * 阻塞绘图的 DMA 分段版本
 * --------------------------------------------------------------------------
 * LCD_Fill() 与 LCD_ShowString() 都是逐像素轮询输出：240x135 整屏填充会独占
 * CPU 约 80ms，足以让主循环里的周期任务（USB-PD 的 500ms 应答窗口）错过时序。
 * 下面两个接口把同样的绘制改成分段 DMA，由 BspLcdService() 每次推进一块，
 * 单次占用时间降到 1ms 量级。
 * ========================================================================== */

/** @brief 每个动态字符串块最多容纳的字符数。 */
#define LCD_DMA_TEXT_CHUNK_CHARS  (8u)

/**
 * @brief 渲染一屏整宽填充的一行，并通过 DMA 送出。
 * @param[in] y       行号（0 起）。
 * @param[in] u16Color RGB565 颜色。
 * @retval E_OK   已启动一次 DMA 传输。
 * @retval E_BUSY 上一次 DMA 未完成。
 * @note  调用前必须已用 LCD_Address_Set() 设置好整个填充区域的窗口，
 *        本函数只追加像素数据，不再改窗口。
 */
eStatusDef LCD_FillRowDma(uint16_t y, uint16_t u16Color);

/**
 * @brief 查询是否仍有待推进的整屏填充。
 * @retval 1 有待完成的行。
 * @retval 0 填充已结束。
 */
uint8_t LCD_FillActive(void);

/**
 * @brief 渲染一段字符串（最多 LCD_DMA_TEXT_CHUNK_CHARS 字符）并通过 DMA 送出。
 * @param[in] x,y   起始坐标。
 * @param[in] p     字符串指针。
 * @param[in] fc,bc 前景/背景色。
 * @param[in] sizey 字号（12/16/24/32）。
 * @retval E_OK    已启动一次 DMA 传输。
 * @retval E_BUSY  上一次 DMA 未完成。
 * @retval E_ERROR 参数非法或字号不支持。
 * @note  本函数自己设置地址窗口；返回值不表示整串画完，调用者按字符推进。
 */
eStatusDef LCD_ShowStringChunkDma(uint16_t x, uint16_t y, const char *p,
                                  uint16_t fc, uint16_t bc, uint8_t sizey);

/* 图片 / 自定义尺寸汉字 */
void LCD_ShowPicture(uint16_t x, uint16_t y, uint16_t length, uint16_t width, const uint8_t pic[]);
void LCD_ShowChineseTEST(uint16_t x, uint16_t y, uint8_t *s, uint16_t fc, uint16_t bc, uint8_t sizeW, uint8_t sizeH, uint8_t mode);

/* ==========================================================================
 * 阻塞式矩形输出
 * --------------------------------------------------------------------------
 * 供 Code/UserApp/ui 的 2bpp 渲染层使用：界面按"独立小矩形"重绘，
 * 不需要整屏帧缓冲。两个接口都是同步的（返回前等 DMA 完成），
 * 单次最多推送 LCD_DMA_BUFFER_BYTES 字节，只能在主循环任务上下文调用。
 * ========================================================================== */

/**
 * @brief  阻塞式填充一个矩形区域
 * @param[in] u16X,u16Y 左上角坐标
 * @param[in] u16W,u16H 宽与高（像素），会按 240x135 屏幕边界裁剪
 * @param[in] u16Color  RGB565 填充色
 */
void LCD_FillRect(uint16_t u16X, uint16_t u16Y, uint16_t u16W, uint16_t u16H, uint16_t u16Color);

/**
 * @brief  阻塞式输出一个 RGB565 像素块到矩形区域
 * @param[in] u16X,u16Y  左上角坐标
 * @param[in] u16W,u16H  宽与高（像素），必须与 pu8Rgb565 内的像素数一致
 * @param[in] pu8Rgb565  像素数据，大端字节序（高字节在前）
 */
void LCD_BlitRect(uint16_t u16X, uint16_t u16Y, uint16_t u16W, uint16_t u16H,
                  const uint8_t *pu8Rgb565);

#endif /* __ST7789V_H__ */
