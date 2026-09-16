/**
 * @file st7789v.c
 * @brief Implements the ST7789V display driver for the CH32X035 SPI interface.
 */

#include "st7789v.h"
#include "font.h"
#define LCD_DMA_FONT_SIZE_Y       (24u)
#define LCD_DMA_FONT_SIZE_X       (LCD_DMA_FONT_SIZE_Y / 2u)
#define LCD_DMA_MAX_DIGITS        (8u)

/**
 * @brief DMA 缓冲大小。
 * @note  保持 4608 字节（8 个 24 号数字 = 8 × 12 × 24 × 2），这是既有
 *        LCD_ShowIntNumAsync()/LCD_ShowFloatNumAsync() 依赖的容量，不能改小。
 *        整屏填充复用它：240x135 每个 DMA 块最多推 9 行（2160 像素），
 *        因此一次调用耗时仍在 1ms 量级，整屏约需 15 次服务。
 */
#define LCD_DMA_BUFFER_BYTES      (LCD_DMA_MAX_DIGITS * LCD_DMA_FONT_SIZE_X * LCD_DMA_FONT_SIZE_Y * 2u)

static uint8_t au8LcdDmaBuffer[LCD_DMA_BUFFER_BYTES];
static volatile uint8_t u8LcdDmaBusy = 0u;

/** @brief 分段填充状态：窗口、当前行、颜色与每次推送的行数。 */
static uint8_t  s_u8FillActive = 0u;
static uint16_t s_u16FillX0 = 0u;
static uint16_t s_u16FillY0 = 0u;
static uint16_t s_u16FillX1 = 0u;
static uint16_t s_u16FillY1 = 0u;
static uint16_t s_u16FillRow = 0u;
static uint16_t s_u16FillColor = 0u;
static uint8_t  s_u8FillRowsPerChunk = 1u;

/**
 * @brief 最小字号的行跨度表（font.h 的 24 号字体）。
 * @note  这些字体是逐行组织的位掩码，每行 (sizey/2 + 7)/8 字节，
 *        位 0 对应最左侧像素——与 LCD_ShowChar() 的取位方式一致。
 */
static const uint8_t *LCD_GetFontTable(uint8_t u8SizeY)
{
    if (u8SizeY == 12u)
    {
        return (const uint8_t *)ascii_1206;
    }
    if (u8SizeY == 16u)
    {
        return (const uint8_t *)ascii_1608;
    }
    if (u8SizeY == 24u)
    {
        return (const uint8_t *)ascii_2412;
    }
    if (u8SizeY == 32u)
    {
        return (const uint8_t *)ascii_3216;
    }
    return 0;
}

/** @brief 透明取字节接口：把二维字体表当成一维字节流读取。 */
static uint8_t LCD_FontByte(const uint8_t *pu8FontTable, uint16_t u16Offset)
{
    return pu8FontTable[u16Offset];
}

/**
 * @brief 渲染一个字符到 DMA 缓冲。
 * @param[out] pu8Buffer 目标缓冲。
 * @param[in]  u32Offset 起始字节偏移。
 * @param[in]  u8Char 字符。
 * @param[in]  u8SizeX 字符宽（sizey/2）。
 * @param[in]  u8SizeY 字符高。
 * @param[in]  u16Fc 前景色；u16Bc 背景色。
 * @return 写入的字节数。
 */
static uint32_t LCD_RenderCharDma(uint8_t *pu8Buffer, uint32_t u32Offset,
                                  uint32_t u32Capacity,
                                  uint8_t u8Char, uint8_t u8SizeX, uint8_t u8SizeY,
                                  uint16_t u16Fc, uint16_t u16Bc)
{
    const uint8_t *pu8FontTable;
    uint16_t u16GlyphBytes;
    uint16_t u16RowBytes;
    uint8_t  u8Row;
    uint8_t  u8Column;
    uint8_t  u8Byte;
    uint32_t u32Index = u32Offset;

    pu8FontTable = LCD_GetFontTable(u8SizeY);
    if (pu8FontTable == 0)
    {
        return u32Offset;
    }

    if ((u8Char < (uint8_t)' ') || (u8Char > (uint8_t)'~'))
    {
        u8Char = (uint8_t)' ';
    }
    u8Char = (uint8_t)(u8Char - (uint8_t)' ');

    u16RowBytes = (uint16_t)((u8SizeX + 7u) / 8u);
    u16GlyphBytes = (uint16_t)(u16RowBytes * u8SizeY);

    /* 缓冲放不下整个字符就不画，避免越界写 */
    if ((u32Index + (uint32_t)u16GlyphBytes * 8u) > u32Capacity)
    {
        return u32Offset;
    }

    for (u8Row = 0u; u8Row < u8SizeY; u8Row++)
    {
        for (u8Column = 0u; u8Column < u8SizeX; u8Column++)
        {
            u8Byte = LCD_FontByte(pu8FontTable,
                                  (uint16_t)((uint16_t)u8Char * u16GlyphBytes +
                                             (uint16_t)u8Row * u16RowBytes +
                                             (uint16_t)(u8Column >> 3)));

            if ((u8Byte & (uint8_t)(1u << (u8Column & 0x07u))) != 0u)
            {
                pu8Buffer[u32Index] = (uint8_t)(u16Fc >> 8u);
                u32Index++;
                pu8Buffer[u32Index] = (uint8_t)u16Fc;
                u32Index++;
            }
            else
            {
                pu8Buffer[u32Index] = (uint8_t)(u16Bc >> 8u);
                u32Index++;
                pu8Buffer[u32Index] = (uint8_t)u16Bc;
                u32Index++;
            }
        }
    }

    return u32Index;
}

static void LCD_DmaWritePixel(uint16_t u16Color, uint32_t *pu32Index)
{
    au8LcdDmaBuffer[*pu32Index] = (uint8_t)(u16Color >> 8u);
    (*pu32Index)++;
    au8LcdDmaBuffer[*pu32Index] = (uint8_t)u16Color;
    (*pu32Index)++;
}

static void LCD_DmaRenderString(const uint8_t *pu8Characters, uint8_t u8Length,
                                uint16_t u16Foreground, uint16_t u16Background,
                                uint32_t *pu32Index)
{
    uint8_t u8Byte;
    uint8_t u8Character;
    uint8_t u8Column;
    uint8_t u8DigitIndex;
    uint8_t u8Row;
    uint8_t u8BytesPerRow;

    u8BytesPerRow = (uint8_t)((LCD_DMA_FONT_SIZE_X + 7u) / 8u);
    for (u8Row = 0u; u8Row < LCD_DMA_FONT_SIZE_Y; u8Row++)
    {
        for (u8DigitIndex = 0u; u8DigitIndex < u8Length; u8DigitIndex++)
        {
            u8Character = pu8Characters[u8DigitIndex];
            if ((u8Character < (uint8_t)' ') || (u8Character > (uint8_t)'~'))
            {
                u8Character = (uint8_t)' ';
            }
            u8Character -= (uint8_t)' ';

            for (u8Column = 0u; u8Column < LCD_DMA_FONT_SIZE_X; u8Column++)
            {
                u8Byte = ascii_2412[u8Character][(uint16_t)u8Row * u8BytesPerRow + u8Column / 8u];
                LCD_DmaWritePixel((u8Byte & (1u << (u8Column % 8u))) != 0u ?
                                  u16Foreground : u16Background, pu32Index);
            }
        }
    }
}
/**
 * @brief Sends repeated bytes through the LCD SPI1 polling interface.
 * @param[in] TxData Byte value to send.
 * @param[in] size Number of repetitions.
 * @retval 0 The bytes were submitted to the board-level SPI interface.
 */
uint8_t SPI_WriteByte(uint8_t TxData, uint16_t size)
{
    uint16_t u16Index;

    for (u16Index = 0u; u16Index < size; u16Index++)
    {
        BspSpiWriteByte(TxData);
    }

    return 0u;
}

/**
 * @brief  向 LCD 写入一个字节的命令
 * @param[in] cmd  命令字节
 */
static void LCD_Write_Cmd(uint8_t cmd)
{
    LCD_DC(CMD);
    SPI_WriteByte(cmd, 1);
}

/**
 * @brief  向 LCD 写入一个字节的数据
 * @param[in] dat  数据字节
 */
static void LCD_Write_Data(uint8_t dat)
{
    LCD_DC(DATA);
    SPI_WriteByte(dat, 1);
}

/**
 * @brief  向 LCD 写入两字节数据（大端序）
 * @param[in] dat  16-bit 数据（先高 8 位，后低 8 位）
 */
void LCD_Write_Data2Bytes(uint16_t dat)
{
    LCD_DC(DATA);
	LCD_Write_Data(dat>>8);
	LCD_Write_Data(dat);
}

/**
 * @brief  设置 LCD 读写地址窗口
 * @param[in] x1  列起始地址
 * @param[in] y1  行起始地址
 * @param[in] x2  列结束地址
 * @param[in] y2  行结束地址
 * @note   根据 USE_HORIZONTAL 自动进行偏移校正
 *         命令序列：0x2a（列地址）→ 0x2b（行地址）→ 0x2c（存储器写）
 */
void LCD_Address_Set(uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2)
{
	if(USE_HORIZONTAL==0)
	{
		LCD_Write_Cmd(0x2a);        // 列地址设置
		LCD_Write_Data2Bytes(x1+52);
		LCD_Write_Data2Bytes(x2+52);
		LCD_Write_Cmd(0x2b);        // 行地址设置
		LCD_Write_Data2Bytes(y1+40);
		LCD_Write_Data2Bytes(y2+40);
		LCD_Write_Cmd(0x2c);        // 存储器写
	}
	else if(USE_HORIZONTAL==1)
	{
		LCD_Write_Cmd(0x2a);        // 列地址设置
		LCD_Write_Data2Bytes(x1+53);
		LCD_Write_Data2Bytes(x2+53);
		LCD_Write_Cmd(0x2b);        // 行地址设置
		LCD_Write_Data2Bytes(y1+40);
		LCD_Write_Data2Bytes(y2+40);
		LCD_Write_Cmd(0x2c);        // 存储器写
	}
	else if(USE_HORIZONTAL==2)
	{
		LCD_Write_Cmd(0x2a);        // 列地址设置
		LCD_Write_Data2Bytes(x1+40);
		LCD_Write_Data2Bytes(x2+40);
		LCD_Write_Cmd(0x2b);        // 行地址设置
		LCD_Write_Data2Bytes(y1+53);
		LCD_Write_Data2Bytes(y2+53);
		LCD_Write_Cmd(0x2c);        // 存储器写
	}
	else
	{
		LCD_Write_Cmd(0x2a);        // 列地址设置
		LCD_Write_Data2Bytes(x1+40);
		LCD_Write_Data2Bytes(x2+40);
		LCD_Write_Cmd(0x2b);        // 行地址设置
		LCD_Write_Data2Bytes(y1+52);
		LCD_Write_Data2Bytes(y2+52);
		LCD_Write_Cmd(0x2c);        // 存储器写
	}
}

/**
 * @brief  设置 LCD 显示方向
 * @param[in] Dir_Mode  方向模式
 *                      0 = 竖屏（正常），1 = 竖屏（翻转）
 *                      2 = 横屏（正常），3 = 横屏（翻转）
 * @note   通过命令 0x36（MADCTL）设置扫描方向和 RGB 顺序
 */
void ST7789V_SetDir(uint8_t Dir_Mode)
{
    LCD_Write_Cmd(0x36); /* 显示方向 */
    if (Dir_Mode == 0)
        LCD_Write_Data(0x00);
    else if (Dir_Mode == 1)
        LCD_Write_Data(0xC0);
    else if (Dir_Mode == 2)
        LCD_Write_Data(0x70);
    else
        LCD_Write_Data(0xA0);
}
/**
 * @brief  在指定坐标写入一个像素点颜色
 * @param[in] x1     x 坐标
 * @param[in] y1     y 坐标
 * @param[in] color  像素颜色（RGB565）
 */
void LCD_color_point(uint16_t x1, uint16_t y1, uint16_t color)
{

    LCD_Address_Set(x1, y1, x1, y1);
    LCD_Write_Data2Bytes(color);
}


/**
 * @brief  指定区域填充单一颜色
 * @param[in] xsta  起始 x 坐标
 * @param[in] ysta  起始 y 坐标
 * @param[in] xend  结束 x 坐标（不含）
 * @param[in] yend  结束 y 坐标（不含）
 * @param[in] color 填充颜色（RGB565）
 * @note   大区域走 DMA 分段输出：每行一次 DMA，由 BspLcdService() 逐行推进，
 *         单次只占用 CPU 约 1ms；小区域仍走原来的轮询路径以避免碎片化。
 */
void LCD_Fill(uint16_t xsta,uint16_t ysta,uint16_t xend,uint16_t yend,uint16_t color)
{
	uint16_t u16Width;
	uint16_t u16RowsByCount;

	if (xend <= xsta || yend <= ysta)
	{
		return;
	}

	u16Width = (uint16_t)(xend - xsta);

	/* 只有"整屏宽"的填充才能一次设置窗口后连续灌多行：因为窗口内的像素是
	 * 按行连续推进的，行宽不等于屏宽时中间会跳掉未填充的部分。 */
	if (u16Width == LCD_W)
	{
		u16RowsByCount = (uint16_t)(LCD_DMA_BUFFER_BYTES / (uint16_t)(u16Width * 2u));
		if (u16RowsByCount == 0u)
		{
			u16RowsByCount = 1u;
		}

		s_u16FillX0 = xsta;
		s_u16FillY0 = ysta;
		s_u16FillX1 = xend;
		s_u16FillY1 = yend;
		s_u16FillColor = color;
		s_u16FillRow = ysta;
		s_u8FillRowsPerChunk = (uint8_t)u16RowsByCount;
		s_u8FillActive = 1u;

		LCD_Address_Set(xsta, ysta, (uint16_t)(xend - 1u), (uint16_t)(yend - 1u));
		return;
	}

	/* 非整屏宽：窗口每次都要重设，因此按"每次一块"处理，块内行数仍受缓冲限制 */
	{
		uint16_t u16PixelsPerChunk = (uint16_t)(LCD_DMA_BUFFER_BYTES / 2u);
		uint16_t u16Rows = (uint16_t)(u16PixelsPerChunk / u16Width);

		if (u16Rows == 0u)
		{
			u16Rows = 1u;
		}

		s_u16FillX0 = xsta;
		s_u16FillY0 = ysta;
		s_u16FillX1 = xend;
		s_u16FillY1 = yend;
		s_u16FillColor = color;
		s_u16FillRow = ysta;
		s_u8FillRowsPerChunk = (uint8_t)u16Rows;
		s_u8FillActive = 1u;
	}
}

/**
 * @brief 推进一块填充的 DMA 输出（块内包含 s_u8FillRowsPerChunk 行）。
 * @retval E_OK   已启动一次 DMA。
 * @retval E_BUSY 上一次 DMA 未完成（下次服务再试）。
 * @retval E_ERROR 当前没有待完成的填充。
 */
eStatusDef LCD_FillRowDma(uint16_t y, uint16_t u16Color)
{
	uint16_t u16Width;
	uint16_t u16Rows;
	uint16_t u16Index;
	uint32_t u32Offset = 0u;
	eStatusDef eStatus;

	(void)y;
	(void)u16Color;

	if (s_u8FillActive == 0u)
	{
		return E_ERROR;
	}

	if (u8LcdDmaBusy != 0u)
	{
		return E_BUSY;
	}

	if (s_u16FillRow >= s_u16FillY1)
	{
		s_u8FillActive = 0u;
		return E_ERROR;
	}

	u16Width = (uint16_t)(s_u16FillX1 - s_u16FillX0);
	u16Rows = s_u8FillRowsPerChunk;
	if ((uint16_t)(s_u16FillRow + u16Rows) > s_u16FillY1)
	{
		u16Rows = (uint16_t)(s_u16FillY1 - s_u16FillRow);
	}

	/* 非整屏宽时每次都要重新定位窗口（连续多行会串到未填充区域） */
	if (u16Width != LCD_W)
	{
		LCD_Address_Set(s_u16FillX0, s_u16FillRow,
		                (uint16_t)(s_u16FillX1 - 1u),
		                (uint16_t)(s_u16FillRow + u16Rows - 1u));
	}

	for (u16Index = 0u; u16Index < (uint16_t)(u16Width * u16Rows); u16Index++)
	{
		au8LcdDmaBuffer[u32Offset] = (uint8_t)(s_u16FillColor >> 8u);
		u32Offset++;
		au8LcdDmaBuffer[u32Offset] = (uint8_t)s_u16FillColor;
		u32Offset++;
	}

	LCD_DC(DATA);
	u8LcdDmaBusy = 1u;
	eStatus = BspSpiWriteBufferDma(au8LcdDmaBuffer, (uint16_t)u32Offset);
	if (eStatus != E_OK)
	{
		u8LcdDmaBusy = 0u;
		return eStatus;
	}

	s_u16FillRow = (uint16_t)(s_u16FillRow + u16Rows);
	if (s_u16FillRow >= s_u16FillY1)
	{
		s_u8FillActive = 0u;
	}

	return E_OK;
}

/**
 * @brief 判断是否仍有待推进的整屏填充。
 * @retval 1 有待完成的行。
 * @retval 0 填充已结束。
 */
uint8_t LCD_FillActive(void)
{
	return s_u8FillActive;
}

/**
 * @brief 渲染一段字符串（最多 LCD_DMA_TEXT_CHUNK_CHARS 字符）并 DMA 送出。
 * @note  地址窗口按整块字符区域设置，像素按行连续输出，与 LCD_ShowString()
 *        的视觉效果一致（非叠加模式、带背景色）。
 */
eStatusDef LCD_ShowStringChunkDma(uint16_t x, uint16_t y, const char *p,
                                  uint16_t fc, uint16_t bc, uint8_t sizey)
{
	uint8_t  u8SizeX;
	uint8_t  u8Count = 0u;
	uint32_t u32Offset = 0u;
	eStatusDef eStatus;

	if ((p == 0) || (sizey == 0u) || ((sizey & 0x01u) != 0u))
	{
		return E_ERROR;
	}
	if (LCD_GetFontTable(sizey) == 0)
	{
		return E_ERROR;
	}
	if (u8LcdDmaBusy != 0u)
	{
		return E_BUSY;
	}

	u8SizeX = (uint8_t)(sizey / 2u);

	while ((p[u8Count] != '\0') && (u8Count < LCD_DMA_TEXT_CHUNK_CHARS))
	{
		uint32_t u32Next;

		u32Next = LCD_RenderCharDma(au8LcdDmaBuffer, u32Offset,
		                            (uint32_t)LCD_DMA_BUFFER_BYTES,
		                            (uint8_t)p[u8Count], u8SizeX, sizey, fc, bc);
		if (u32Next == u32Offset)
		{
			break;   /* 缓冲放不下下一个字符 */
		}
		u32Offset = u32Next;
		u8Count++;
	}

	if (u8Count == 0u)
	{
		return E_ERROR;
	}

	LCD_Address_Set(x, y, (uint16_t)(x + (uint16_t)u8Count * u8SizeX - 1u),
	                (uint16_t)(y + sizey - 1u));

	LCD_DC(DATA);
	u8LcdDmaBusy = 1u;
	eStatus = BspSpiWriteBufferDma(au8LcdDmaBuffer, (uint16_t)u32Offset);
	if (eStatus != E_OK)
	{
		u8LcdDmaBusy = 0u;
	}

	return eStatus;
}

/**
 * @brief  ST7789V LCD 初始化
 * @note   执行完整的初始化序列，包括：
 *         - 硬件复位（RESET 引脚时序）
 *         - 退出睡眠模式（SLPOUT，命令 0x11）
 *         - 显示方向设置（MADCTL，命令 0x36）
 *         - 色彩格式设置（COLMOD，命令 0x3A — 16-bit RGB565）
 *         - 帧率设置（命令 0xB2 / 0xB7）
 *         - 电源设置（命令 0xBB / 0xC0~0xC6 / 0xD0）
 *         - 正负 Gamma 校正（命令 0xE0 / 0xE1）
 *         - 显示开启（DISPON，命令 0x29）
 *         初始化完成后显示启动画面
 */
void st7789v_init(void)
{
    printf("[LCD] ST7789V init begin\r\n");
    LCD_CS(0);
    Delay_Ms(100);
    LCD_RST(1);
    Delay_Ms(100);
    LCD_RST(0);
    Delay_Ms(100);
    LCD_RST(1);
    Delay_Ms(100);
    printf("[LCD] hardware reset released\r\n");

    /* 退出睡眠模式 */
    LCD_Write_Cmd(0x11);
    Delay_Ms(120);
    printf("[LCD] sleep-out sent\r\n");
    ST7789V_SetDir(USE_HORIZONTAL);
//    LCD_Write_Cmd(0x36);
//    LCD_Write_Data(0x00);



    LCD_Write_Cmd(0x3a);
    LCD_Write_Data(0x05); /* Match the verified module's 16-bit control-interface format. */
    //--------------------------------ST7789V Frame rate setting-----------------

    LCD_Write_Cmd(0xb2);
    LCD_Write_Data(0x0c);
    LCD_Write_Data(0x0c);
    LCD_Write_Data(0x00);
    LCD_Write_Data(0x33);
    LCD_Write_Data(0x33);
    LCD_Write_Cmd(0xb7);
    LCD_Write_Data(0x35);
    //---------------------------------ST7789V Power setting---------------------

    LCD_Write_Cmd(0xbb);
    LCD_Write_Data(0x19); // 0x28
    LCD_Write_Cmd(0xc0);
    LCD_Write_Data(0x2c);
    LCD_Write_Cmd(0xc2);
    LCD_Write_Data(0x01);
    LCD_Write_Cmd(0xc3);
    LCD_Write_Data(0x12); // 0x10
    LCD_Write_Cmd(0xc4);
    LCD_Write_Data(0x20);
    LCD_Write_Cmd(0xc6);
    LCD_Write_Data(0x0f);
    LCD_Write_Cmd(0xd0);
    LCD_Write_Data(0xa4);
    LCD_Write_Data(0xa1);
    //--------------------------------ST7789V gamma setting----------------------

    LCD_Write_Cmd(0xe0);
    LCD_Write_Data(0xd0);
    LCD_Write_Data(0x04); // 0x00
    LCD_Write_Data(0x0d); // 0x02
    LCD_Write_Data(0x11); // 0x07
    LCD_Write_Data(0x13); // 0x0a
    LCD_Write_Data(0x2b); // 0x28
    LCD_Write_Data(0x3f); // 0x32
    LCD_Write_Data(0x54); // 0x44
    LCD_Write_Data(0x4c); // 0x42
    LCD_Write_Data(0x18); // 0x06
    LCD_Write_Data(0x0d); // 0x0e
    LCD_Write_Data(0x0b); // 0x12
    LCD_Write_Data(0x1f); // 0x14
    LCD_Write_Data(0x23); // 0x17
    LCD_Write_Cmd(0xe1);
    LCD_Write_Data(0xd0); // 0xd0
    LCD_Write_Data(0x04); // 0x00
    LCD_Write_Data(0x0c); // 0x02
    LCD_Write_Data(0x11); // 0x07
    LCD_Write_Data(0x13); // 0x0a
    LCD_Write_Data(0x2c); // 0x28
    LCD_Write_Data(0x3f); // 0x31
    LCD_Write_Data(0x44); // 0x54
    LCD_Write_Data(0x51); // 0x47
    LCD_Write_Data(0x2f); // 0x0e
    LCD_Write_Data(0x1f); // 0x1c
    LCD_Write_Data(0x1f); // 0x17
    LCD_Write_Data(0x20); // 0x1b
    LCD_Write_Data(0x23); // 0x1e

    LCD_Write_Cmd(0x21);
    LCD_Write_Cmd(0x29);
    Delay_Ms(20);
    printf("[LCD] display-on sent, filling red test background\r\n");

    LCD_Fill(0, 0, 240, 135, RED);
    /* LCD_Fill() 现在只登记填充请求、由 BspLcdService() 分批 DMA 输出；
     * 初始化阶段调度器还没起来，这里自己把它推完。 */
    while (LCD_FillActive() != 0u)
    {
        uint32_t u32Guard = 40000000u;

        if (LCD_FillRowDma(0u, RED) == E_BUSY)
        {
            /* 等当前这块 DMA 传完再推下一块 */
            while ((LCD_IsTransferBusy() != 0u) && (u32Guard != 0u))
            {
                u32Guard--;
            }
            if (u32Guard == 0u)
            {
                break;   /* 异常保护：不允许在初始化里死等 */
            }
        }
    }
    printf("[LCD] init complete, spi_error=%u\r\n", (unsigned int)BspSpiHasError());
}



/**
 * @brief  在指定位置画一个像素点
 * @param[in] x      x 坐标
 * @param[in] y      y 坐标
 * @param[in] color  点的颜色（RGB565）
 */
void LCD_DrawPoint(uint16_t x,uint16_t y,uint16_t color)
{
	LCD_Address_Set(x,y,x,y);//设置光标位置 
	LCD_Write_Data2Bytes(color);
} 


/**
 * @brief  画线（Bresenham 算法）
 * @param[in] x1,y1  起点坐标
 * @param[in] x2,y2  终点坐标
 * @param[in] color  线的颜色（RGB565）
 */
void LCD_DrawLine(uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2,uint16_t color)
{
	uint16_t t; 
	int xerr=0,yerr=0,delta_x,delta_y,distance;
	int incx,incy,uRow,uCol;
	delta_x=x2-x1; //计算坐标增量 
	delta_y=y2-y1;
	uRow=x1;//画线起点坐标
	uCol=y1;
	if(delta_x>0)incx=1; //设置单步方向 
	else if (delta_x==0)incx=0;//垂直线 
	else {incx=-1;delta_x=-delta_x;}
	if(delta_y>0)incy=1;
	else if (delta_y==0)incy=0;//水平线 
	else {incy=-1;delta_y=-delta_y;}
	if(delta_x>delta_y)distance=delta_x; //选取基本增量坐标轴 
	else distance=delta_y;
	for(t=0;t<distance+1;t++)
	{
		LCD_DrawPoint(uRow,uCol,color);//画点
		xerr+=delta_x;
		yerr+=delta_y;
		if(xerr>distance)
		{
			xerr-=distance;
			uRow+=incx;
		}
		if(yerr>distance)
		{
			yerr-=distance;
			uCol+=incy;
		}
	}
}


/**
 * @brief  画空心矩形
 * @param[in] x1,y1  左上角坐标
 * @param[in] x2,y2  右下角坐标
 * @param[in] color  边框颜色（RGB565）
 */
void LCD_DrawRectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2,uint16_t color)
{
	LCD_DrawLine(x1,y1,x2,y1,color);
	LCD_DrawLine(x1,y1,x1,y2,color);
	LCD_DrawLine(x1,y2,x2,y2,color);
	LCD_DrawLine(x2,y1,x2,y2,color);
}


/**
 * @brief  画空心圆（Bresenham 算法）
 * @param[in] x0,y0  圆心坐标
 * @param[in] r      半径（像素）
 * @param[in] color  圆的颜色（RGB565）
 */
void Draw_Circle(uint16_t x0,uint16_t y0,uint8_t r,uint16_t color)
{
	int a,b;
	a=0;b=r;	  
	while(a<=b)
	{
		LCD_DrawPoint(x0-b,y0-a,color);             //3           
		LCD_DrawPoint(x0+b,y0-a,color);             //0           
		LCD_DrawPoint(x0-a,y0+b,color);             //1                
		LCD_DrawPoint(x0-a,y0-b,color);             //2             
		LCD_DrawPoint(x0+b,y0+a,color);             //4               
		LCD_DrawPoint(x0+a,y0-b,color);             //5
		LCD_DrawPoint(x0+a,y0+b,color);             //6 
		LCD_DrawPoint(x0-b,y0+a,color);             //7
		a++;
		if((a*a+b*b)>(r*r))//判断要画的点是否过远
		{
			b--;
		}
	}
}



/**
 * @brief  显示单个 12×12 汉字
 * @param[in] x,y    显示坐标
 * @param[in] s      汉字指针（2 字节 GB2312 编码）
 * @param[in] fc     字体颜色
 * @param[in] bc     背景色
 * @param[in] sizey  字号（固定 12）
 * @param[in] mode   0 = 非叠加模式（带背景），1 = 叠加模式（不覆盖背景）
 */
void LCD_ShowChinese12x12(uint16_t x,uint16_t y,uint8_t *s,uint16_t fc,uint16_t bc,uint8_t sizey,uint8_t mode)
{
	uint8_t i,j,m=0;
	uint16_t k;
	uint16_t HZnum;//汉字数目
	uint16_t TypefaceNum;//一个字符所占字节大小
	uint16_t x0=x;
	TypefaceNum=(sizey/8+((sizey%8)?1:0))*sizey;
	                         
	HZnum=sizeof(tfont12)/sizeof(typFNT_GB12);	//统计汉字数目
	for(k=0;k<HZnum;k++) 
	{
		if((tfont12[k].Index[0]==*(s))&&(tfont12[k].Index[1]==*(s+1)))
		{ 	
			LCD_Address_Set(x,y,x+sizey-1,y+sizey-1);
			for(i=0;i<TypefaceNum;i++)
			{
				for(j=0;j<8;j++)
				{	
					if(!mode)//非叠加方式
					{
						if(tfont12[k].Msk[i]&(0x01<<j))LCD_Write_Data2Bytes(fc);
						else LCD_Write_Data2Bytes(bc);
						m++;
						if(m%sizey==0)
						{
							m=0;
							break;
						}
					}
					else//叠加方式
					{
						if(tfont12[k].Msk[i]&(0x01<<j))	LCD_DrawPoint(x,y,fc);//画一个点
						x++;
						if((x-x0)==sizey)
						{
							x=x0;
							y++;
							break;
						}
					}
				}
			}
		}				  	
		continue;  //查找到对应点阵字库立即退出，防止多个汉字重复取模带来影响
	}
} 

/**
 * @brief  显示单个 16×16 汉字
 * @param[in] x,y    显示坐标
 * @param[in] s      汉字指针（2 字节 GB2312 编码）
 * @param[in] fc     字体颜色
 * @param[in] bc     背景色
 * @param[in] sizey  字号（固定 16）
 * @param[in] mode   0 = 非叠加模式，1 = 叠加模式
 */
void LCD_ShowChinese16x16(uint16_t x,uint16_t y,uint8_t *s,uint16_t fc,uint16_t bc,uint8_t sizey,uint8_t mode)
{
	uint8_t i,j,m=0;
	uint16_t k;
	uint16_t HZnum;//汉字数目
	uint16_t TypefaceNum;//一个字符所占字节大小
	uint16_t x0=x;
  TypefaceNum=(sizey/8+((sizey%8)?1:0))*sizey;
	HZnum=sizeof(tfont16)/sizeof(typFNT_GB16);	//统计汉字数目
	for(k=0;k<HZnum;k++) 
	{
		if ((tfont16[k].Index[0]==*(s))&&(tfont16[k].Index[1]==*(s+1)))
		{ 	
			LCD_Address_Set(x,y,x+sizey-1,y+sizey-1);
			for(i=0;i<TypefaceNum;i++)
			{
				for(j=0;j<8;j++)
				{	
					if(!mode)//非叠加方式
					{
						if(tfont16[k].Msk[i]&(0x01<<j))LCD_Write_Data2Bytes(fc);
						else LCD_Write_Data2Bytes(bc);
						m++;
						if(m%sizey==0)
						{
							m=0;
							break;
						}
					}
					else//叠加方式
					{
						if(tfont16[k].Msk[i]&(0x01<<j))	LCD_DrawPoint(x,y,fc);//画一个点
						x++;
						if((x-x0)==sizey)
						{
							x=x0;
							y++;
							break;
						}
					}
				}
			}
		}				  	
		continue;  //查找到对应点阵字库立即退出，防止多个汉字重复取模带来影响
	}
} 


/**
 * @brief  显示单个 24×24 汉字
 * @param[in] x,y    显示坐标
 * @param[in] s      汉字指针（2 字节 GB2312 编码）
 * @param[in] fc     字体颜色
 * @param[in] bc     背景色
 * @param[in] sizey  字号（固定 24）
 * @param[in] mode   0 = 非叠加模式，1 = 叠加模式
 */
void LCD_ShowChinese24x24(uint16_t x,uint16_t y,uint8_t *s,uint16_t fc,uint16_t bc,uint8_t sizey,uint8_t mode)
{
	uint8_t i,j,m=0;
	uint16_t k;
	uint16_t HZnum;//汉字数目
	uint16_t TypefaceNum;//一个字符所占字节大小
	uint16_t x0=x;
	TypefaceNum=(sizey/8+((sizey%8)?1:0))*sizey;
	HZnum=sizeof(tfont24)/sizeof(typFNT_GB24);	//统计汉字数目
	for(k=0;k<HZnum;k++) 
	{
		if ((tfont24[k].Index[0]==*(s))&&(tfont24[k].Index[1]==*(s+1)))
		{ 	
			LCD_Address_Set(x,y,x+sizey-1,y+sizey-1);
			for(i=0;i<TypefaceNum;i++)
			{
				for(j=0;j<8;j++)
				{	
					if(!mode)//非叠加方式
					{
						if(tfont24[k].Msk[i]&(0x01<<j))LCD_Write_Data2Bytes(fc);
						else LCD_Write_Data2Bytes(bc);
						m++;
						if(m%sizey==0)
						{
							m=0;
							break;
						}
					}
					else//叠加方式
					{
						if(tfont24[k].Msk[i]&(0x01<<j))	LCD_DrawPoint(x,y,fc);//画一个点
						x++;
						if((x-x0)==sizey)
						{
							x=x0;
							y++;
							break;
						}
					}
				}
			}
		}				  	
		continue;  //查找到对应点阵字库立即退出，防止多个汉字重复取模带来影响
	}
} 

/**
 * @brief  显示单个 32×32 汉字
 * @param[in] x,y    显示坐标
 * @param[in] s      汉字指针（2 字节 GB2312 编码）
 * @param[in] fc     字体颜色
 * @param[in] bc     背景色
 * @param[in] sizey  字号（固定 32）
 * @param[in] mode   0 = 非叠加模式，1 = 叠加模式
 */
void LCD_ShowChinese32x32(uint16_t x,uint16_t y,uint8_t *s,uint16_t fc,uint16_t bc,uint8_t sizey,uint8_t mode)
{
	uint8_t i,j,m=0;
	uint16_t k;
	uint16_t HZnum;//汉字数目
	uint16_t TypefaceNum;//一个字符所占字节大小
	uint16_t x0=x;
	TypefaceNum=(sizey/8+((sizey%8)?1:0))*sizey;
	HZnum=sizeof(tfont32)/sizeof(typFNT_GB32);	//统计汉字数目
	for(k=0;k<HZnum;k++) 
	{
		if ((tfont32[k].Index[0]==*(s))&&(tfont32[k].Index[1]==*(s+1)))
		{ 	
			// printf("32:%d,%d,%x,%X\n",HZnum,TypefaceNum,tfontTEST[k].Index[0],*(s));
			LCD_Address_Set(x,y,x+sizey-1,y+sizey-1);
			for(i=0;i<TypefaceNum;i++)
			{
				for(j=0;j<8;j++)
				{	
					if(!mode)//非叠加方式
					{
						if(tfont32[k].Msk[i]&(0x01<<j))LCD_Write_Data2Bytes(fc);
						else LCD_Write_Data2Bytes(bc);
						m++;
						if(m%sizey==0)
						{
							m=0;
							break;
						}
					}
					else//叠加方式
					{
						if(tfont32[k].Msk[i]&(0x01<<j))	LCD_DrawPoint(x,y,fc);//画一个点
						x++;
						if((x-x0)==sizey)
						{
							x=x0;
							y++;
							break;
						}
					}
				}
			}
		}				  	
		continue;  //查找到对应点阵字库立即退出，防止多个汉字重复取模带来影响
	}
}

/**
 * @brief  显示自定义尺寸汉字（用于特殊大字显示）
 * @param[in] x,y      显示坐标
 * @param[in] s        汉字字符串
 * @param[in] fc       字体颜色
 * @param[in] bc       背景色
 * @param[in] sizeW    字符宽度
 * @param[in] sizeH    字符高度
 * @param[in] mode     0 = 非叠加模式，1 = 叠加模式
 */
void LCD_ShowChineseTEST(uint16_t x,uint16_t y,uint8_t *s,uint16_t fc,uint16_t bc,uint8_t sizeW,uint8_t sizeH,uint8_t mode)
{
	uint16_t i,j,m=0;
	uint16_t k;
	uint16_t HZnum;//汉字数目
	uint16_t TypefaceNum;//一个字符所占字节大小
	uint16_t x0=x;
	TypefaceNum=(sizeW*2/16)*sizeH;
	HZnum=sizeof(tfontTEST)/sizeof(typFNT_GBTEST);	//统计汉字数目
	for(k=0;k<HZnum;k++) 
	{
		// printf("%d,%d,%x,%X\n",HZnum,TypefaceNum,tfontTEST[k].Index[0],*(s));
		if ((tfontTEST[k].Index[0]==*(s+k)))
		{ 	
			// printf("%d,%d,%x,%X\n",HZnum,TypefaceNum,tfontTEST[k].Index[0],*(s+k));
			LCD_Address_Set(x+sizeW*k,y,x+(80*(k+1))-1,y+(135)-1);
			for(i=0;i<TypefaceNum;i++)
			{
				for(j=0;j<8;j++)
				{	
					// if(tfontTEST[k].Msk[i]&(0x01<<j))LCD_Write_Data2Bytes(fc);
					// else LCD_Write_Data2Bytes(bc);
					// m++;
					// if(m%sizeW==0)
					// {
					// 	m=0;
					// 	break;
					// }

					if(!mode)//非叠加方式
					{
						if(tfontTEST[k].Msk[i]&(0x01<<j))LCD_Write_Data2Bytes(fc);
						else LCD_Write_Data2Bytes(bc);
						m++;
						if(m%sizeW==0)
						{
							m=0;
							break;
						}
					}
					else//叠加方式
					{
						if(tfontTEST[k].Msk[i]&(0x01<<j))	LCD_DrawPoint(x,y,fc);//画一个点
						x++;
						if((x-x0)==sizeW)
						{
							x=x0;
							y++;
							break;
						}
					}


				}
			}




		}				  	
		continue;  //查找到对应点阵字库立即退出，防止多个汉字重复取模带来影响
	}
}

/**
 * @brief  显示汉字字符串（根据字号自动选择对应字库）
 * @param[in] x,y    显示坐标
 * @param[in] s      汉字字符串（以 '\0' 结尾，GB2312 编码）
 * @param[in] fc     字体颜色
 * @param[in] bc     背景色
 * @param[in] sizey  字号：12/16/24/32
 * @param[in] mode   0 = 非叠加模式，1 = 叠加模式
 */
void LCD_ShowChinese(uint16_t x,uint16_t y,uint8_t *s,uint16_t fc,uint16_t bc,uint8_t sizey,uint8_t mode)
{
	while(*s!=0)
	{
		if(sizey==12) LCD_ShowChinese12x12(x,y,s,fc,bc,sizey,mode);
		else if(sizey==16) LCD_ShowChinese16x16(x,y,s,fc,bc,sizey,mode);
		else if(sizey==24) LCD_ShowChinese24x24(x,y,s,fc,bc,sizey,mode);
		else if(sizey==32) LCD_ShowChinese32x32(x,y,s,fc,bc,sizey,mode);
		else return;
		s+=2;
		x+=sizey;
	}
}

/**
 * @brief  显示一个 ASCII 字符
 * @param[in] x,y    显示坐标
 * @param[in] num    要显示的字符（ASCII 码）
 * @param[in] fc     字体颜色
 * @param[in] bc     背景色
 * @param[in] sizey  字号（12/16/24/32，对应 6×12 / 8×16 / 12×24 / 16×32）
 * @param[in] mode   0 = 非叠加模式（带背景），1 = 叠加模式（不覆盖背景）
 */
void LCD_ShowChar(uint16_t x,uint16_t y,uint8_t num,uint16_t fc,uint16_t bc,uint8_t sizey,uint8_t mode)
{
	uint8_t temp,sizex,t,m=0;
	uint16_t i,TypefaceNum;//一个字符所占字节大小
	uint16_t x0=x;
	sizex=sizey/2;
	TypefaceNum=(sizex/8+((sizex%8)?1:0))*sizey;
	num=num-' ';    //得到偏移后的值
	LCD_Address_Set(x,y,x+sizex-1,y+sizey-1);  //设置光标位置 
	for(i=0;i<TypefaceNum;i++)
	{ 
		if(sizey==12)temp=ascii_1206[num][i];		       //调用6x12字体
		else if(sizey==16)temp=ascii_1608[num][i];		 //调用8x16字体
		else if(sizey==24)temp=ascii_2412[num][i];		 //调用12x24字体
		else if(sizey==32)temp=ascii_3216[num][i];		 //调用16x32字体
		else return;
		for(t=0;t<8;t++)
		{
			if(!mode)//非叠加模式
			{
				if(temp&(0x01<<t))LCD_Write_Data2Bytes(fc);
				else LCD_Write_Data2Bytes(bc);
				m++;
				if(m%sizex==0)
				{
					m=0;
					break;
				}
			}
			else//叠加模式
			{
				if(temp&(0x01<<t))LCD_DrawPoint(x,y,fc);//画一个点
				x++;
				if((x-x0)==sizex)
				{
					x=x0;
					y++;
					break;
				}
			}
		}
	}   	 	  
}


/**
 * @brief  显示 ASCII 字符串
 * @param[in] x,y    显示坐标
 * @param[in] p      字符串指针（以 '\0' 结尾）
 * @param[in] fc     字体颜色
 * @param[in] bc     背景色
 * @param[in] sizey  字号
 * @param[in] mode   0 = 非叠加模式，1 = 叠加模式
 */
void LCD_ShowString(uint16_t x,uint16_t y,const uint8_t *p,uint16_t fc,uint16_t bc,uint8_t sizey,uint8_t mode)
{         
	while(*p!='\0')
	{       
		LCD_ShowChar(x,y,*p,fc,bc,sizey,mode);
		x+=sizey/2;
		p++;
	}  
}


/**
 * @brief  整数幂运算
 * @param[in] m  底数
 * @param[in] n  指数
 * @return m 的 n 次幂
 * @note   用于数字显示时的位权计算
 */
uint32_t mypow(uint8_t m,uint8_t n)
{
	uint32_t result=1;	 
	while(n--)result*=m;
	return result;
}


/**
 * @brief  显示无符号整数（不显示前导零）
 * @param[in] x,y    显示坐标
 * @param[in] num    待显示整数
 * @param[in] len    显示位数
 * @param[in] fc     字体颜色
 * @param[in] bc     背景色
 * @param[in] sizey  字号
 * @note   高位为零时显示空格，避免前导零，至少显示一位数字
 */
void LCD_ShowIntNum(uint16_t x,uint16_t y,uint16_t num,uint8_t len,uint16_t fc,uint16_t bc,uint8_t sizey)
{         	
	uint8_t t,temp;
	uint8_t enshow=0;
	uint8_t sizex=sizey/2;
	for(t=0;t<len;t++)
	{
		temp=(num/mypow(10,len-t-1))%10;
		if(enshow==0&&t<(len-1))
		{
			if(temp==0)
			{
				LCD_ShowChar(x+t*sizex,y,' ',fc,bc,sizey,0);
				continue;
			}else enshow=1; 
		 	 
		}
	 	LCD_ShowChar(x+t*sizex,y,temp+48,fc,bc,sizey,0);
	}
} 


/**
 * @brief  显示浮点数（保留两位小数）
 * @param[in] x,y    显示坐标
 * @param[in] num    待显示浮点数
 * @param[in] len    显示总位数（含小数点）
 * @param[in] fc     字体颜色
 * @param[in] bc     背景色
 * @param[in] sizey  字号
 * @note   将浮点数放大 100 倍后按整数显示，在倒数第二位前插入小数点
 */
void LCD_ShowFloatNum1(uint16_t x,uint16_t y,float num,uint8_t len,uint16_t fc,uint16_t bc,uint8_t sizey)
{         	
	uint8_t t,temp,sizex;
	uint16_t num1;
	sizex=sizey/2;
	num1=num*100;
	for(t=0;t<len;t++)
	{
		temp=(num1/mypow(10,len-t-1))%10;
		if(t==(len-2))
		{
			LCD_ShowChar(x+(len-2)*sizex,y,'.',fc,bc,sizey,0);
			t++;
			len+=1;
		}
	 	LCD_ShowChar(x+t*sizex,y,temp+48,fc,bc,sizey,0);
	}
}


/**
 * @brief  显示 RGB565 格式图片
 * @param[in] x,y      图片左上角坐标
 * @param[in] length   图片宽度（像素）
 * @param[in] width    图片高度（像素）
 * @param[in] pic[]    图片数据数组（RGB565 格式，每像素 2 字节）
 * @note   数据排列：先高字节后低字节，逐点逐行扫描
 */
void LCD_ShowPicture(uint16_t x,uint16_t y,uint16_t length,uint16_t width,const uint8_t pic[])
{
	uint16_t i,j;
	uint32_t k=0;
	LCD_Address_Set(x,y,x+length-1,y+width-1);
	for(i=0;i<length;i++)
	{
		for(j=0;j<width;j++)
		{
			LCD_Write_Data(pic[k*2]);
			LCD_Write_Data(pic[k*2+1]);
			k++;
		}
	}			
}

/**
 * @brief Renders an integer into the shared buffer and starts DMA transmission.
 * @param[in] x Left coordinate.
 * @param[in] y Top coordinate.
 * @param[in] num Unsigned value to display.
 * @param[in] len Field width in characters.
 * @param[in] fc Foreground RGB565 color.
 * @param[in] bc Background RGB565 color.
 * @param[in] sizey Font height; only 24 pixels is supported.
 * @retval E_OK The DMA transfer was started.
 * @retval E_BUSY The shared DMA buffer is in use.
 * @retval E_ERROR A parameter is invalid or SPI has failed.
 */
eStatusDef LCD_ShowIntNumAsync(uint16_t x, uint16_t y, uint32_t num, uint8_t len,
                               uint16_t fc, uint16_t bc, uint8_t sizey)
{
    uint8_t au8Characters[LCD_DMA_MAX_DIGITS];
    uint8_t u8Digit;
    uint8_t u8ShowDigit = 0u;
    uint8_t u8Index;
    uint32_t u32BufferIndex = 0u;
    eStatusDef eStatus;

    if ((sizey != LCD_DMA_FONT_SIZE_Y) || (len == 0u) || (len > LCD_DMA_MAX_DIGITS) ||
        ((uint32_t)x + (uint32_t)len * LCD_DMA_FONT_SIZE_X > LCD_W) ||
        ((uint32_t)y + LCD_DMA_FONT_SIZE_Y > LCD_H))
    {
        return E_ERROR;
    }

    if ((u8LcdDmaBusy != 0u) || (BspSpiIsIdle() == 0u))
    {
        return E_BUSY;
    }

    for (u8Index = 0u; u8Index < len; u8Index++)
    {
        u8Digit = (uint8_t)((num / mypow(10u, (uint8_t)(len - u8Index - 1u))) % 10u);
        if ((u8ShowDigit == 0u) && (u8Index < (len - 1u)) && (u8Digit == 0u))
        {
            au8Characters[u8Index] = (uint8_t)' ';
        }
        else
        {
            u8ShowDigit = 1u;
            au8Characters[u8Index] = (uint8_t)('0' + u8Digit);
        }
    }
    LCD_DmaRenderString(au8Characters, len, fc, bc, &u32BufferIndex);
    LCD_Address_Set(x, y, (uint16_t)(x + len * LCD_DMA_FONT_SIZE_X - 1u),
                    (uint16_t)(y + LCD_DMA_FONT_SIZE_Y - 1u));
    LCD_DC(DATA);
    u8LcdDmaBusy = 1u;
    eStatus = BspSpiWriteBufferDma(au8LcdDmaBuffer, (uint16_t)u32BufferIndex);
    if (eStatus != E_OK)
    {
        u8LcdDmaBusy = 0u;
    }
    return eStatus;
}

/**
 * @brief Renders a fixed-point value and starts DMA transmission.
 * @param[in] x Left coordinate.
 * @param[in] y Top coordinate.
 * @param[in] fValue Floating-point value to display.
 * @param[in] u8Length Field width including sign and decimal point.
 * @param[in] u8Decimals Number of decimal places.
 * @param[in] fc Foreground RGB565 color.
 * @param[in] bc Background RGB565 color.
 * @param[in] sizey Font height; only 24 pixels is supported.
 * @retval E_OK The DMA transfer was started.
 * @retval E_BUSY The shared DMA buffer is in use.
 * @retval E_ERROR A parameter is invalid or SPI has failed.
 */
eStatusDef LCD_ShowFloatNumAsync(uint16_t x, uint16_t y, float fValue,
                                 uint8_t u8Length, uint8_t u8Decimals,
                                 uint16_t fc, uint16_t bc, uint8_t sizey)
{
    uint8_t au8Characters[LCD_DMA_MAX_DIGITS];
    uint8_t au8Digits[LCD_DMA_MAX_DIGITS];
    uint8_t u8Index;
    uint8_t u8Digit;
    uint8_t u8ShowDigit = 0u;
    uint8_t u8DotPos;
    uint8_t u8IntLen;
    uint8_t u8Pos;
    uint8_t u8Neg = 0u;
    uint8_t u8TotalDigits;
    uint32_t u32Scaled;
    uint32_t u32Scale = 1u;
    uint32_t u32BufferIndex = 0u;
    eStatusDef eStatus;

    if ((sizey != LCD_DMA_FONT_SIZE_Y) || (u8Length == 0u) || (u8Length > LCD_DMA_MAX_DIGITS) ||
        (u8Decimals == 0u) || ((uint8_t)(u8Decimals + 1u) >= u8Length) ||
        ((uint32_t)x + (uint32_t)u8Length * LCD_DMA_FONT_SIZE_X > LCD_W) ||
        ((uint32_t)y + LCD_DMA_FONT_SIZE_Y > LCD_H))
    {
        return E_ERROR;
    }

    if ((u8LcdDmaBusy != 0u) || (BspSpiIsIdle() == 0u))
    {
        return E_BUSY;
    }

    if (fValue < 0.0f)
    {
        u8Neg = 1u;
        fValue = -fValue;
    }

    for (u8Index = 0u; u8Index < u8Decimals; u8Index++)
    {
        u32Scale *= 10u;
    }
    u32Scaled = (uint32_t)(fValue * (float)u32Scale + 0.5f);

    u8DotPos = (uint8_t)(u8Length - u8Decimals - 1u);
    u8IntLen = u8DotPos;
    u8TotalDigits = (uint8_t)(u8IntLen + u8Decimals);

    /* 逐位提取（LSB 在前） */
    for (u8Index = 0u; u8Index < u8TotalDigits; u8Index++)
    {
        au8Digits[u8Index] = (uint8_t)(u32Scaled % 10u);
        u32Scaled /= 10u;
    }

    /* 从左到右填充字符 */
    u8ShowDigit = 0u;
    for (u8Pos = 0u; u8Pos < u8Length; u8Pos++)
    {
        if (u8Pos == u8DotPos)
        {
            au8Characters[u8Pos] = (uint8_t)'.';
        }
        else if (u8Pos < u8DotPos)
        {
            /* 整数部分：MSB 在 au8Digits[u8TotalDigits-1] */
            u8Digit = au8Digits[(uint8_t)(u8TotalDigits - 1u - u8Pos)];
            if ((u8ShowDigit == 0u) && (u8Digit == 0u) && (u8Pos < (uint8_t)(u8IntLen - 1u)))
            {
                if (u8Neg != 0u)
                {
                    au8Characters[u8Pos] = (uint8_t)'-';
                    u8Neg = 0u;
                    u8ShowDigit = 1u;
                }
                else
                {
                    au8Characters[u8Pos] = (uint8_t)' ';
                }
            }
            else
            {
                u8ShowDigit = 1u;
                au8Characters[u8Pos] = (uint8_t)('0' + u8Digit);
            }
        }
        else
        {
            /* 小数部分：始终显示 */
            u8Digit = au8Digits[(uint8_t)(u8Length - 1u - u8Pos)];
            au8Characters[u8Pos] = (uint8_t)('0' + u8Digit);
            u8ShowDigit = 1u;
        }
    }

    LCD_DmaRenderString(au8Characters, u8Length, fc, bc, &u32BufferIndex);
    LCD_Address_Set(x, y, (uint16_t)(x + u8Length * LCD_DMA_FONT_SIZE_X - 1u),
                    (uint16_t)(y + LCD_DMA_FONT_SIZE_Y - 1u));
    LCD_DC(DATA);
    u8LcdDmaBusy = 1u;
    eStatus = BspSpiWriteBufferDma(au8LcdDmaBuffer, (uint16_t)u32BufferIndex);
    if (eStatus != E_OK)
    {
        u8LcdDmaBusy = 0u;
    }
    return eStatus;
}

/**
 * @brief Reports whether an LCD field DMA transfer is active.
 * @retval 1 A transfer is active.
 * @retval 0 The LCD DMA buffer is available.
 */
uint8_t LCD_IsTransferBusy(void)
{
    if ((u8LcdDmaBusy != 0u) && (BspSpiIsIdle() != 0u))
    {
        u8LcdDmaBusy = 0u;
    }

    return u8LcdDmaBusy;
}

/* ==========================================================================
 * 阻塞式矩形输出（供 Code/UserApp/ui 的 2bpp 渲染层使用）
 * --------------------------------------------------------------------------
 * ui 层的绘制单元都是"独立的小矩形"（单个字形最大 16x17，数值矩形最大
 * 109x54），尺寸可控，因此这里不接入 BspLcdService 的字段队列，而是
 * "设一次窗口 + 循环 DMA"，由调用方在自己的任务上下文中同步完成：
 *   - 单次 DMA 最多 LCD_DMA_BUFFER_BYTES 字节（约 3ms @12MHz SPI），
 *     调用之间会等待 DMA 空闲，不会和字段队列抢 DMA；
 *   - 数值矩形只在内容变化时才重绘（约 5Hz），CPU 占用可忽略；
 *   - 仅用于主循环任务上下文，禁止在中断中调用。
 * ========================================================================== */

/**
 * @brief 等待 LCD 的 TX DMA 空闲
 * @note  超时上限复用 SPI 轮询计数，避免 DMA 异常时死等。
 */
static void LCD_WaitDmaIdle(void)
{
    uint32_t u32Timeout = BSP_SPI_TIMEOUT_COUNT;

    while ((LCD_IsTransferBusy() != 0u) && (u32Timeout != 0u))
    {
        u32Timeout--;
    }
}

/**
 * @brief  阻塞式填充一个矩形区域
 * @param[in] u16X,u16Y 左上角坐标
 * @param[in] u16W,u16H 宽与高（像素），会按屏幕边界裁剪
 * @param[in] u16Color  RGB565 填充色
 * @note   窗口只设置一次，之后按 DMA 缓冲容量分块连续灌像素；
 *         ST7789V 在 0x2C 之后会自动在窗口内递增地址，因此分块是安全的。
 */
void LCD_FillRect(uint16_t u16X, uint16_t u16Y, uint16_t u16W, uint16_t u16H, uint16_t u16Color)
{
	uint16_t u16ChunkPixels;
	uint16_t u16Pixels;
	uint32_t u32Remaining;
	uint32_t u32Index;

	if ((u16W == 0u) || (u16H == 0u))
	{
		return;
	}
	if ((u16X >= LCD_W) || (u16Y >= LCD_H))
	{
		return;
	}
	if (((uint32_t)u16X + u16W) > LCD_W)
	{
		u16W = (uint16_t)(LCD_W - u16X);
	}
	if (((uint32_t)u16Y + u16H) > LCD_H)
	{
		u16H = (uint16_t)(LCD_H - u16Y);
	}

	LCD_WaitDmaIdle();

	/* DMA 缓冲整体预填充为同一颜色，之后每块只重启 DMA */
	u16ChunkPixels = (uint16_t)(LCD_DMA_BUFFER_BYTES / 2u);
	for (u32Index = 0u; u32Index < u16ChunkPixels; u32Index++)
	{
		au8LcdDmaBuffer[u32Index * 2u] = (uint8_t)(u16Color >> 8u);
		au8LcdDmaBuffer[u32Index * 2u + 1u] = (uint8_t)u16Color;
	}

	LCD_Address_Set(u16X, u16Y, (uint16_t)(u16X + u16W - 1u), (uint16_t)(u16Y + u16H - 1u));
	LCD_DC(DATA);

	u32Remaining = (uint32_t)u16W * (uint32_t)u16H;
	while (u32Remaining != 0u)
	{
		u16Pixels = (u32Remaining > u16ChunkPixels) ? u16ChunkPixels : (uint16_t)u32Remaining;

		u8LcdDmaBusy = 1u;
		if (BspSpiWriteBufferDma(au8LcdDmaBuffer, (uint16_t)(u16Pixels * 2u)) != E_OK)
		{
			u8LcdDmaBusy = 0u;
			return;
		}

		u32Remaining -= u16Pixels;
		LCD_WaitDmaIdle();
	}
}

/**
 * @brief  阻塞式输出一个 RGB565 像素块到矩形区域
 * @param[in] u16X,u16Y   左上角坐标
 * @param[in] u16W,u16H   宽与高（像素），必须与 pu8Rgb565 内的像素数一致
 * @param[in] pu8Rgb565   像素数据，大端字节序（高字节在前），与 LCD 写入顺序一致
 * @note   数据直接从调用方缓冲 DMA，不做拷贝；函数返回前会等 DMA 完成，
 *         因此调用方缓冲在本次调用期间保持有效即可。
 */
void LCD_BlitRect(uint16_t u16X, uint16_t u16Y, uint16_t u16W, uint16_t u16H,
                  const uint8_t *pu8Rgb565)
{
	uint32_t u32Remaining;
	uint16_t u16ChunkBytes;
	uint16_t u16Len;

	if ((pu8Rgb565 == 0) || (u16W == 0u) || (u16H == 0u))
	{
		return;
	}
	if ((u16X >= LCD_W) || (u16Y >= LCD_H))
	{
		return;
	}
	if (((uint32_t)u16X + u16W) > LCD_W)
	{
		u16W = (uint16_t)(LCD_W - u16X);
	}
	if (((uint32_t)u16Y + u16H) > LCD_H)
	{
		u16H = (uint16_t)(LCD_H - u16Y);
	}

	LCD_WaitDmaIdle();

	LCD_Address_Set(u16X, u16Y, (uint16_t)(u16X + u16W - 1u), (uint16_t)(u16Y + u16H - 1u));
	LCD_DC(DATA);

	u16ChunkBytes = (uint16_t)LCD_DMA_BUFFER_BYTES;
	u32Remaining = (uint32_t)u16W * (uint32_t)u16H * 2u;
	while (u32Remaining != 0u)
	{
		if (u32Remaining > u16ChunkBytes)
		{
			/* 分块时按像素对齐，避免把一个像素拆到两次 DMA */
			u16Len = (uint16_t)(u16ChunkBytes & 0xFFFEu);
		}
		else
		{
			u16Len = (uint16_t)u32Remaining;
		}

		u8LcdDmaBusy = 1u;
		if (BspSpiWriteBufferDma(pu8Rgb565, u16Len) != E_OK)
		{
			u8LcdDmaBusy = 0u;
			return;
		}

		pu8Rgb565 += u16Len;
		u32Remaining -= u16Len;
		LCD_WaitDmaIdle();
	}
}
