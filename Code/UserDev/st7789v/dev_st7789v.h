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


/* 像素发送方式（编译期二选一）
 *   1 = DMA 异步：FillRectStart 只登记矩形，返回 1 表示忙，由 DevSt7789vService
 *                 逐行推出，调用方下一拍重试；主循环必须注册 DevSt7789vTask。
 *   0 = 阻塞逐像素：FillRectStart 当场发完才返回，永远返回 0；
 *                 DevSt7789vService 变成空转。 */
#define ST7789V_USE_DMA   (1)


/* DC 引脚电平：区分命令与数据 */
#define ST7789V_DC_CMD   0
#define ST7789V_DC_DATA  1

/* ST7789V 命令码 */
#define ST7789V_CMD_CASET  0x2a   /* 列地址设置 */
#define ST7789V_CMD_RASET  0x2b   /* 行地址设置 */
#define ST7789V_CMD_RAMWR  0x2c   /* 存储器写 */


/** @brief 显示方向：0/1=竖屏(135×240)，2/3=横屏(240×135) */
#define ST7789V_USE_HORIZONTAL  3

#if ST7789V_USE_HORIZONTAL==0||ST7789V_USE_HORIZONTAL==1
#define ST7789V_WIDTH   135
#define ST7789V_HEIGHT  240
#else
#define ST7789V_WIDTH   240
#define ST7789V_HEIGHT  135
#endif

/* 地址窗口偏移校正：宏名后缀数字对应 ST7789V_USE_HORIZONTAL 的取值 */
#define ST7789V_OFFSET_X_0  52
#define ST7789V_OFFSET_Y_0  40
#define ST7789V_OFFSET_X_1  53
#define ST7789V_OFFSET_Y_1  40
#define ST7789V_OFFSET_X_2  40
#define ST7789V_OFFSET_Y_2  53
#define ST7789V_OFFSET_X_3  40
#define ST7789V_OFFSET_Y_3  52

#define ST7789V_PIXEL_NUM  (ST7789V_WIDTH * ST7789V_HEIGHT)

/* 颜色定义（RGB565 格式） */
#define ST7789V_WHITE          (uint16_t)0xFFFF
#define ST7789V_BLACK          (uint16_t)0x0000
#define ST7789V_BLUE           (uint16_t)0x001F
#define ST7789V_BRED           (uint16_t)0XF81F
#define ST7789V_GRED           (uint16_t)0XFFE0
#define ST7789V_GBLUE          (uint16_t)0X07FF
#define ST7789V_RED            (uint16_t)0xF800
#define ST7789V_MAGENTA        (uint16_t)0xF81F
#define ST7789V_GREEN          (uint16_t)0x07E0
#define ST7789V_CYAN           (uint16_t)0x7FFF
#define ST7789V_YELLOW         (uint16_t)0xFFE0
#define ST7789V_BROWN          (uint16_t)0XBC40
#define ST7789V_BRRED          (uint16_t)0XFC07
#define ST7789V_GRAY           (uint16_t)0X8430
#define ST7789V_DARKBLUE       (uint16_t)0X01CF
#define ST7789V_LIGHTBLUE      (uint16_t)0X7D7C
#define ST7789V_GRAYBLUE       (uint16_t)0X5458
#define ST7789V_LIGHTGREEN     (uint16_t)0X841F
#define ST7789V_LGRAY          (uint16_t)0XC618
#define ST7789V_LGRAYBLUE      (uint16_t)0XA651
#define ST7789V_LBBLUE         (uint16_t)0X2B12


/* 外部函数声明 */
void DevSt7789vInit(void);
uint8_t  DevSt7789vFillRectStart(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t u16Color);
uint8_t  DevSt7789vFillScreenStart(uint16_t u16Color);
void     DevSt7789vService(void);
uint16_t DevSt7789vTask(void);

#endif /* __ST7789V_H__ */
