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
#include "Components/font/font.h"


/* 像素发送方式（编译期二选一）
 *   1 = DMA 异步：绘图接口只写入当前帧列表，DevSt7789vShow 提交后由
 *                 DevSt7789vService 逐行推出；主循环必须注册 DevSt7789vTask。
 *   0 = 阻塞逐像素：绘图接口当场发完才返回，DevSt7789vShow 无需等待；
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

/* 颜色定义（RGB565 格式）
 * 注释里写的是这个值实际显示出来的颜色，不是宏名的字面意思。
 * 标注"名字不符"的几个是从原厂示例驱动带过来的，名字和颜色对不上。 */
#define COLOR_WHITE          (uint16_t)0xFFFF   /* 白     #FFFFFF */
#define COLOR_BLACK          (uint16_t)0x0000   /* 黑     #000000 */
#define COLOR_BLUE           (uint16_t)0x001F   /* 纯蓝   #0000FF */
#define COLOR_BRED           (uint16_t)0XF81F   /* 品红   #FF00FF  名字不符，与 MAGENTA 同值 */
#define COLOR_GRED           (uint16_t)0XFFE0   /* 纯黄   #FFFF00  名字不符，与 YELLOW 同值 */
#define COLOR_GBLUE          (uint16_t)0X07FF   /* 青     #00FFFF  名字不符 */
#define COLOR_RED            (uint16_t)0xF800   /* 纯红   #FF0000 */
#define COLOR_MAGENTA        (uint16_t)0xF81F   /* 品红   #FF00FF */
#define COLOR_GREEN          (uint16_t)0x07E0   /* 纯绿   #00FF00 */
#define COLOR_CYAN           (uint16_t)0x7FFF   /* 浅青   #7BFFFF  比标准青亮一档 */
#define COLOR_YELLOW         (uint16_t)0xFFE0   /* 纯黄   #FFFF00 */
#define COLOR_BROWN          (uint16_t)0XBC40   /* 棕     #BD8600 */
#define COLOR_BRRED          (uint16_t)0XFC07   /* 橙红   #FF8239 */
#define COLOR_GRAY           (uint16_t)0X8430   /* 灰     #848684 */
#define COLOR_DARKBLUE       (uint16_t)0X01CF   /* 深蓝   #001C7B */
#define COLOR_LIGHTBLUE      (uint16_t)0X7D7C   /* 浅青蓝 #7BD7E7 */
#define COLOR_GRAYBLUE       (uint16_t)0X5458   /* 蓝灰   #528AC6 */
#define COLOR_LIGHTGREEN     (uint16_t)0X841F   /* 紫蓝   #8482FF  名字不符，不是绿色 */
#define COLOR_LGRAY          (uint16_t)0XC618   /* 浅灰   #C6C3C6 */
#define COLOR_LGRAYBLUE      (uint16_t)0XA651   /* 浅黄绿 #A5CB8C  名字不符，实际偏绿 */
#define COLOR_LBBLUE         (uint16_t)0X2B12   /* 中蓝   #296194 */


/* 外部函数声明 */

/**
 * @brief 行内容回调：为矩形内第 u16Row 行填 u16W 个像素
 * @param[in] u16Row   矩形内行号，0 = 矩形顶边
 * @param[in] u16W     矩形宽度（像素）
 * @param[out] pu16Line 输出缓冲，填"原始 RGB565"，字节序由发送端转
 * @note  回调在 DevSt7789vService() 里被调用（主循环上下文）。
 *        里面只填像素，不要再发起刷新、等待或打日志。
 */
typedef void (*DevSt7789vRowFn)(uint16_t u16Row, uint16_t u16W, uint16_t *pu16Line);

void DevSt7789vInit(void);
uint8_t  DevSt7789vFillRectStart(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t u16Color);
uint8_t  DevSt7789vBlitRectStart(uint16_t x, uint16_t y, uint16_t w, uint16_t h, DevSt7789vRowFn pfnRow);
uint8_t  DevSt7789vFillScreenStart(uint16_t u16Color);
uint8_t  DevSt7789vShowImg(const uint8_t *pu8Img);
uint8_t  DevSt7789vDrawText(int16_t i16X, int16_t i16BaselineY, const tFont *ptFont,
                            uint16_t u16Fg, uint16_t u16Bg, const char *pcText);
uint8_t  DevSt7789vShow(void);
void     DevSt7789vService(void);
uint16_t DevSt7789vTask(void);

#endif /* __ST7789V_H__ */
