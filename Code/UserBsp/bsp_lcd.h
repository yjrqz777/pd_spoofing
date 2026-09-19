/**
 * @file    bsp_lcd.h
 * @brief   LCD 显示底层驱动头文件
 *******************************************************************************
 * @note    在 ST7789V 绘图接口之上提供"字段缓冲式刷新"：
 *          应用层（user_display.c）先把一帧要显示的内容追加进请求队列，
 *          再由 BspLcdService() 按 DMA 就绪节奏逐条输出。
 *
 *          数据流：
 *            BspLcdBeginRefresh() -> BspLcdAddXxx()... -> (每时间片) BspLcdService()
 *******************************************************************************
 */

#ifndef __BSP_LCD_H__
#define __BSP_LCD_H__

#ifdef __cplusplus
extern "C" {
#endif



/**
 * @brief  初始化 LCD 显示屏
 * @note   调用 ST7789V 初始化序列（硬件复位、SLPOUT、方向、Gamma、DISPON），
 *         返回前会把整屏刷成红色作为通信测试。
 * @warning 该函数包含复位延时与一次整屏填充，仅在上电初始化时调用。
 */
void BspLcdInit(void);

/**
 * @brief  把整个屏幕填充为指定颜色
 * @param[in] u16Color  RGB565 颜色
 * @note   非阻塞：登记一条填充操作后立即返回，实际像素由 BspLcdService()
 *         分批 DMA 输出（整屏约 15 次服务，每次约 1ms CPU）。
 * @warning 若队列里已有未接管的请求内容，本函数会先丢弃它们。
 */
void BspLcdClearScreen(uint16_t u16Color);

/**
 * @brief  丢弃尚未被接管的请求帧。
 * @note   供"清屏 + 重画"这类需要独占一帧的场合使用；不影响正在输出的活动帧。
 */
void BspLcdCancelRefresh(void);

/**
 * @brief  查询因请求帧槽位不足被丢弃的操作数。
 * @return 累计丢弃数量。不为 0 表示界面上有内容没能显示出来，
 *         需要调大 BspLcdService 所在文件里的 BSP_LCD_FIELD_MAX。
 */
uint16_t BspLcdGetDroppedOps(void);

/* ===================== 阻塞式直接显示接口 ===================== */

void BspLcdShowString(uint16_t u16X, uint16_t u16Y, const char *pcText,
                      uint16_t u16Fc, uint16_t u16Bc, uint8_t u8SizeY, uint8_t u8Mode);

void BspLcdShowUInt(uint16_t u16X, uint16_t u16Y, uint32_t u32Value,
                    uint8_t u8Length, uint16_t u16Fc, uint16_t u16Bc);

void BspLcdShowFloat(uint16_t u16X, uint16_t u16Y, float f32Value, uint8_t u8Length,
                     uint8_t u8Decimals, uint16_t u16Fc, uint16_t u16Bc);

/* ===================== 字段缓冲式分时刷新 ===================== */

/**
 * @brief  开启一轮刷新收集
 * @param[in] u8StateId  当前系统状态标识（状态切换时自动丢弃过期帧）
 */
void BspLcdBeginRefresh(uint8_t u8StateId);

/** @brief 追加：清屏填充 */
void BspLcdAddFill(uint16_t u16Color);

/** @brief 追加：字符串（DMA 异步输出，按 LCD_DMA_TEXT_CHUNK_CHARS 分块） */
void BspLcdAddString(uint16_t u16X, uint16_t u16Y, const char *pcText,
                     uint16_t u16Fc, uint16_t u16Bc, uint8_t u8SizeY);

/** @brief 追加：无符号整数（DMA 异步输出） */
void BspLcdAddUInt(uint16_t u16X, uint16_t u16Y, uint32_t u32Value,
                   uint8_t u8Length, uint16_t u16Color);

/** @brief 追加：浮点数（DMA 异步输出） */
void BspLcdAddFloat(uint16_t u16X, uint16_t u16Y, float f32Value,
                    uint8_t u8Length, uint8_t u8Decimals, uint16_t u16Color);

/**
 * @brief  字段刷新服务：按 DMA 就绪节奏逐条输出。
 * @param[in] u8StateId  当前状态标识，与发起刷新时不一致则丢弃过期帧
 * @note   每个时间片调用一次；整帧输出完毕后自动等待下一轮 BeginRefresh。
 */
void BspLcdService(uint8_t u8StateId);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_LCD_H__ */
