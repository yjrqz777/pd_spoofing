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

#ifndef __DEV_ST7789V_H__
#define __DEV_ST7789V_H__
#include "UserBsp/bsp_spi.h"
#include "user_global.h"


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
void DevSt7789vInit(void);
// extern void LCD_color_point(uint16_t x1, uint16_t y1, uint16_t color);

#endif /* __ST7789V_H__ */
