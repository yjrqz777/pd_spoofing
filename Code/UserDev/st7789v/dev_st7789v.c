

#include "dev_st7789v.h"
#include "Components/font/img_xj_bw.h"

/* 点阵尺寸必须和当前屏幕方向一致，对不上就用 tools/picture/img2bit.py 重新生成 */
#if (ST7789V_WIDTH != IMG_XJ_BW_WIDTH) || (ST7789V_HEIGHT != IMG_XJ_BW_HEIGHT)
#error "img_xj_bw 尺寸与 ST7789V_WIDTH/HEIGHT 不一致，请重新生成 img_xj_bw.h/.c"
#endif

/** @brief 一帧中的一个绘制项（模块内部，不对外暴露） */
#define ST7789V_DRAW_ITEM_NUM  (16u)
#define ST7789V_TEXT_LEN       (12u)

typedef struct
{
    uint16_t        u16X;      /* 矩形位置与大小 */
    uint16_t        u16Y;
    uint16_t        u16W;
    uint16_t        u16Color;  /* pfnRow 为 0 时用：整块同一个颜色 */
    DevSt7789vRowFn pfnRow;    /* 0 = 纯色；非 0 = 每行内容由回调给 */
    const tFont    *ptFont;    /* pfnRow 为 TextRowFn 时用：文本来源 */
    const char     *pcText;
    char            acText[ST7789V_TEXT_LEN]; /* 异步发送期间使用的文本副本 */
    uint16_t        u16Fg;
    uint16_t        u16Bg;
    int16_t         i16PenX;
    int16_t         i16BaseY;
    int16_t         i16X0;
    int16_t         i16Y0;
    uint16_t        u16Row;    /* 已发出的行数，从 0 起 */
    uint16_t        u16Left;   /* 剩余行数，0 = 这一项发完了 */
} tSt7789vJobDef;

static uint16_t aLine[ST7789V_WIDTH];   /* 480 字节行缓冲，屏幕专属，不对外暴露 */

#if (ST7789V_USE_DMA != 0)
static tSt7789vJobDef tDrawItems[ST7789V_DRAW_ITEM_NUM];
static uint8_t u8DrawCount = 0u;        /* Show 前已经写入的绘制项数量 */
static uint8_t u8SendIndex = 0u;        /* Show 后当前发送项 */
static uint8_t u8Showing = 0u;          /* 1 = 当前帧正在异步发送 */
#endif

/* TextRowFn 读的那组量：每行开始前，从当前这块 job 搬过来 */
static const tFont *ptTextFont = 0;
static const char  *pcTextStr = 0;
static uint16_t     u16TextFg = 0u;
static uint16_t     u16TextBg = 0u;
static int16_t      i16TextPenX = 0;
static int16_t      i16TextBaseY = 0;
static int16_t      i16TextX0 = 0;   /* 文本墨迹包围盒左上角 */
static int16_t      i16TextY0 = 0;

static void St7789vSendCmd(uint8_t cmd)
{
    BspSpiDc(ST7789V_DC_CMD);
    BspSpiSendByte(cmd);
}

static void St7789vSendData(uint8_t dat)
{
    BspSpiDc(ST7789V_DC_DATA);
    BspSpiSendByte(dat);
}


void St7789vSendData2Bytes(uint16_t dat)
{
    BspSpiDc(ST7789V_DC_DATA);
	St7789vSendData(dat>>8);
	St7789vSendData(dat);
}

/**
 * @brief  设置 LCD 读写地址窗口
 * @param[in] x1  列起始地址
 * @param[in] y1  行起始地址
 * @param[in] x2  列结束地址
 * @param[in] y2  行结束地址
 * @note   根据 ST7789V_USE_HORIZONTAL 自动进行偏移校正
 *         命令序列：0x2a（列地址）→ 0x2b（行地址）→ 0x2c（存储器写）
 */
void St7789vSetAddress(uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2)
{
	if(ST7789V_USE_HORIZONTAL==0)
	{
		St7789vSendCmd(ST7789V_CMD_CASET);        // 列地址设置
		St7789vSendData2Bytes(x1+ST7789V_OFFSET_X_0);
		St7789vSendData2Bytes(x2+ST7789V_OFFSET_X_0);
		St7789vSendCmd(ST7789V_CMD_RASET);        // 行地址设置
		St7789vSendData2Bytes(y1+ST7789V_OFFSET_Y_0);
		St7789vSendData2Bytes(y2+ST7789V_OFFSET_Y_0);
		St7789vSendCmd(ST7789V_CMD_RAMWR);        // 存储器写
	}
	else if(ST7789V_USE_HORIZONTAL==1)
	{
		St7789vSendCmd(ST7789V_CMD_CASET);        // 列地址设置
		St7789vSendData2Bytes(x1+ST7789V_OFFSET_X_1);
		St7789vSendData2Bytes(x2+ST7789V_OFFSET_X_1);
		St7789vSendCmd(ST7789V_CMD_RASET);        // 行地址设置
		St7789vSendData2Bytes(y1+ST7789V_OFFSET_Y_1);
		St7789vSendData2Bytes(y2+ST7789V_OFFSET_Y_1);
		St7789vSendCmd(ST7789V_CMD_RAMWR);        // 存储器写
	}
	else if(ST7789V_USE_HORIZONTAL==2)
	{
		St7789vSendCmd(ST7789V_CMD_CASET);        // 列地址设置
		St7789vSendData2Bytes(x1+ST7789V_OFFSET_X_2);
		St7789vSendData2Bytes(x2+ST7789V_OFFSET_X_2);
		St7789vSendCmd(ST7789V_CMD_RASET);        // 行地址设置
		St7789vSendData2Bytes(y1+ST7789V_OFFSET_Y_2);
		St7789vSendData2Bytes(y2+ST7789V_OFFSET_Y_2);
		St7789vSendCmd(ST7789V_CMD_RAMWR);        // 存储器写
	}
	else
	{
		St7789vSendCmd(ST7789V_CMD_CASET);        // 列地址设置
		St7789vSendData2Bytes(x1+ST7789V_OFFSET_X_3);
		St7789vSendData2Bytes(x2+ST7789V_OFFSET_X_3);
		St7789vSendCmd(ST7789V_CMD_RASET);        // 行地址设置
		St7789vSendData2Bytes(y1+ST7789V_OFFSET_Y_3);
		St7789vSendData2Bytes(y2+ST7789V_OFFSET_Y_3);
		St7789vSendCmd(ST7789V_CMD_RAMWR);        // 存储器写
	}
}

/**
 * @brief  设置 LCD 显示方向
 * @param[in] Dir_Mode  方向模式
 *                      0 = 竖屏（正常），1 = 竖屏（翻转）
 *                      2 = 横屏（正常），3 = 横屏（翻转）
 * @note   通过命令 0x36（MADCTL）设置扫描方向和 RGB 顺序
 */
void St7789vSetDir(uint8_t dir)
{
    St7789vSendCmd(0x36); /* 显示方向 */
	switch (dir)
	{
	case 0:
		St7789vSendData(0x00);
		break;
	case 1:
		St7789vSendData(0xC0);
		break;
	case 2:
		St7789vSendData(0x70);
		break;
	case 3:
		St7789vSendData(0xA0);
		break;
	default:
		St7789vSendData(0x00);
		break;
	}
}
/**
 * @brief  在指定坐标写入一个像素点颜色
 * @param[in] x1     x 坐标
 * @param[in] y1     y 坐标
 * @param[in] color  像素颜色（RGB565）
 */
void DevSt7789vDrawPoint(uint16_t x1, uint16_t y1, uint16_t color)
{
    St7789vSetAddress(x1, y1, x1, y1);
    St7789vSendData2Bytes(color);
}


// void DevSt7789vFillRect(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
// {
// 	uint32_t i;
// 	St7789vSetAddress(x1, y1, x2, y2);
// 	for (i = 0; i < (x2 - x1 + 1) * (y2 - y1 + 1); i++)
// 	{
// 		St7789vSendData2Bytes(color);
// 	}
// }
void DevSt7789vFillRect(uint16_t color)
{
	uint32_t i;
	St7789vSetAddress(0, 0, ST7789V_WIDTH - 1, ST7789V_HEIGHT - 1);
	for (i = 0; i < (ST7789V_WIDTH) * (ST7789V_HEIGHT); i++)
	{
		St7789vSendData2Bytes(color);
	}
}





// /**
//  * @brief  画线（Bresenham 算法）
//  * @param[in] x1,y1  起点坐标
//  * @param[in] x2,y2  终点坐标
//  * @param[in] color  线的颜色（RGB565）
//  */
void DevSt7789vDrawLine(uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2,uint16_t color)
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
		DevSt7789vDrawPoint(uRow,uCol,color);//画点
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


// /**
//  * @brief  画空心矩形
//  * @param[in] x1,y1  左上角坐标
//  * @param[in] x2,y2  右下角坐标
//  * @param[in] color  边框颜色（RGB565）
//  */
void DevSt7789vDrawRectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2,uint16_t color)
{
	DevSt7789vDrawLine(x1,y1,x2,y1,color);
	DevSt7789vDrawLine(x1,y1,x1,y2,color);
	DevSt7789vDrawLine(x1,y2,x2,y2,color);
	DevSt7789vDrawLine(x2,y1,x2,y2,color);
}


// /**
//  * @brief  画空心圆（Bresenham 算法）
//  * @param[in] x0,y0  圆心坐标
//  * @param[in] r      半径（像素）
//  * @param[in] color  圆的颜色（RGB565）
//  */
void DevSt7789vDrawCircle(uint16_t x0,uint16_t y0,uint8_t r,uint16_t color)
{
	int a,b;
	a=0;b=r;	  
	while(a<=b)
	{
		DevSt7789vDrawPoint(x0-b,y0-a,color);             //3           
		DevSt7789vDrawPoint(x0+b,y0-a,color);             //0           
		DevSt7789vDrawPoint(x0-a,y0+b,color);             //1                
		DevSt7789vDrawPoint(x0-a,y0-b,color);             //2             
		DevSt7789vDrawPoint(x0+b,y0+a,color);             //4               
		DevSt7789vDrawPoint(x0+a,y0-b,color);             //5
		DevSt7789vDrawPoint(x0+a,y0+b,color);             //6 
		DevSt7789vDrawPoint(x0-b,y0+a,color);             //7
		a++;
		if((a*a+b*b)>(r*r))//判断要画的点是否过远
		{
			b--;
		}
	}
}

/** @brief 当前正在显示的 1bpp 黑白点阵，按行连续存放 */
static const uint8_t *pu8BwImg = 0;

/**
 * @brief  1bpp 黑白点阵的行内容回调：把第 u16Row 行展开成黑/白像素
 * @param[in]  u16Row   行号，0 = 图像顶边
 * @param[in]  u16W     该行宽度（像素）
 * @param[out] pu16Line 输出缓冲，填原始 RGB565
 * @note   每行 IMG_XJ_BW_ROW_BYTES 字节，高位在左，1 = 白、0 = 黑。
 *         因为矩形从 (0,0) 起，u16Row 就是图像行号。
 */
static void BwImgRowFn(uint16_t u16Row, uint16_t u16W, uint16_t *pu16Line)
{
    const uint8_t *pu8Row;
    uint16_t       i;
    uint8_t        u8Bits = 0u;

    if (pu8BwImg == 0)
    {
        return;
    }

    /* 定位到点阵里第 u16Row 行的首字节：点阵按行连续存放，每行
       IMG_XJ_BW_ROW_BYTES 字节，所以行首 = 基址 + 行号 * 每行字节数。
       先转 uint32_t 再乘：行号偏大时 u16Row * 30 会在 16 位里溢出。 */
    pu8Row = pu8BwImg + ((uint32_t)u16Row * IMG_XJ_BW_ROW_BYTES);

    for (i = 0u; i < u16W; i++)
    {
        if ((i & 7u) == 0u)                      /* i 是 8 的倍数：该换新的一字节了 */
        {
            u8Bits = *pu8Row;                    /* 取当前字节，里面装着 8 个像素 */
            pu8Row++;                            /* 游标前移，后 8 个像素用下一字节 */
        }

        /* 高位在左：判 bit7 就得到最左边那个像素；取完左移一位，
           让下一个像素升到 bit7，于是每轮都能用同一个 0x80 去判。 */
        pu16Line[i] = ((u8Bits & 0x80u) != 0u) ? COLOR_WHITE : COLOR_BLACK;
        u8Bits = (uint8_t)(u8Bits << 1);
    }
}

/**
 * @brief  整屏显示一张 1bpp 黑白点阵图
 * @param[in] pu8Img 点阵数据，按行连续存放，每行 IMG_XJ_BW_ROW_BYTES 字节
 * @retval 0 已写入当前帧；1 参数为空、正在发送或绘制列表已满
 * @note   只把这一块排进当前帧，DMA 方式下还要再调 DevSt7789vShow() 才开始发送。
 *         想在图片上叠字，先调本函数再调 DevSt7789vDrawText() 即可。
 */
uint8_t DevSt7789vShowImg(const uint8_t *pu8Img)
{
    if (pu8Img == 0)
    {
        return 1u;
    }

    pu8BwImg = pu8Img;

    return DevSt7789vBlitRectStart(0u, 0u, ST7789V_WIDTH, ST7789V_HEIGHT, BwImgRowFn);
}


void DevSt7789vInit(void)
{

    BspSpiCs(0);
    Delay_Ms(100);
    BspSpiRst(1);
    Delay_Ms(100);
    BspSpiRst(0);
    Delay_Ms(100);
    BspSpiRst(1);
    Delay_Ms(100);

    /* 退出睡眠模式 */
    St7789vSendCmd(0x11);
    Delay_Ms(120);
    St7789vSetDir(ST7789V_USE_HORIZONTAL);
    St7789vSendCmd(0x3a);
    St7789vSendData(0x05); /* Match the verified module's 16-bit control-interface format. */
    //--------------------------------ST7789V Frame rate setting-----------------

    St7789vSendCmd(0xb2);
    St7789vSendData(0x0c);
    St7789vSendData(0x0c);
    St7789vSendData(0x00);
    St7789vSendData(0x33);
    St7789vSendData(0x33);
    St7789vSendCmd(0xb7);
    St7789vSendData(0x35);
    //---------------------------------ST7789V Power setting---------------------

    St7789vSendCmd(0xbb);
    St7789vSendData(0x19); // 0x28
    St7789vSendCmd(0xc0);
    St7789vSendData(0x2c);
    St7789vSendCmd(0xc2);
    St7789vSendData(0x01);
    St7789vSendCmd(0xc3);
    St7789vSendData(0x12); // 0x10
    St7789vSendCmd(0xc4);
    St7789vSendData(0x20);
    St7789vSendCmd(0xc6);
    St7789vSendData(0x0f);
    St7789vSendCmd(0xd0);
    St7789vSendData(0xa4);
    St7789vSendData(0xa1);
    //--------------------------------ST7789V gamma setting----------------------

    St7789vSendCmd(0xe0);
    St7789vSendData(0xd0);
    St7789vSendData(0x04); // 0x00
    St7789vSendData(0x0d); // 0x02
    St7789vSendData(0x11); // 0x07
    St7789vSendData(0x13); // 0x0a
    St7789vSendData(0x2b); // 0x28
    St7789vSendData(0x3f); // 0x32
    St7789vSendData(0x54); // 0x44
    St7789vSendData(0x4c); // 0x42
    St7789vSendData(0x18); // 0x06
    St7789vSendData(0x0d); // 0x0e
    St7789vSendData(0x0b); // 0x12
    St7789vSendData(0x1f); // 0x14
    St7789vSendData(0x23); // 0x17
    St7789vSendCmd(0xe1);
    St7789vSendData(0xd0); // 0xd0
    St7789vSendData(0x04); // 0x00
    St7789vSendData(0x0c); // 0x02
    St7789vSendData(0x11); // 0x07
    St7789vSendData(0x13); // 0x0a
    St7789vSendData(0x2c); // 0x28
    St7789vSendData(0x3f); // 0x31
    St7789vSendData(0x44); // 0x54
    St7789vSendData(0x51); // 0x47
    St7789vSendData(0x2f); // 0x0e
    St7789vSendData(0x1f); // 0x1c
    St7789vSendData(0x1f); // 0x17
    St7789vSendData(0x20); // 0x1b
    St7789vSendData(0x23); // 0x1e

    St7789vSendCmd(0x21);
    St7789vSendCmd(0x29);
    Delay_Ms(20);

    /* 先铺品红底色，再把启动图叠上去，最后一起提交。
       顺序不能反：两块都排进同一帧后 Show() 才会开始发送，
       否则底色会把图片盖掉。 */
	(void)DevSt7789vFillScreenStart(COLOR_MAGENTA);
    (void)DevSt7789vShowImg(gau8ImgXjBw[0]);
    (void)DevSt7789vShow();
    // DevSt7789vFillRect(ST7789V_RED);
	// DevSt7789vDrawPoint(5, 5, ST7789V_BLUE);
	// DevSt7789vDrawCircle((ST7789V_WIDTH/2), (ST7789V_HEIGHT/2), (ST7789V_HEIGHT/2), ST7789V_GREEN);
}



/**
 * @brief  矩形参数检查：非空且完全落在屏内
 * @retval 0 合法；1 拒绝
 */
static uint8_t RectCheck(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    if ((w == 0u) || (h == 0u))
    {
        return 1u;
    }

    if (((uint32_t)x + w) > ST7789V_WIDTH)
    {
        return 1u;
    }

    if (((uint32_t)y + h) > ST7789V_HEIGHT)
    {
        return 1u;
    }

    return 0u;
}

#if (ST7789V_USE_DMA != 0)
/**
 * @brief  在当前帧的绘制列表中申请一个绘制项
 * @retval 0 参数无效、正在发送或列表已满；非 0 是可填写的绘制项
 */
static tSt7789vJobDef *DrawItemAlloc(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    tSt7789vJobDef *pJob;

    if ((RectCheck(x, y, w, h) != 0u) ||
        (u8Showing != 0u) ||
        (u8DrawCount >= ST7789V_DRAW_ITEM_NUM))
    {
        return 0;
    }

    pJob = &tDrawItems[u8DrawCount];
    pJob->u16X     = x;
    pJob->u16Y     = y;
    pJob->u16W     = w;
    pJob->u16Row   = 0u;
    pJob->u16Left  = h;
    pJob->u16Color = 0u;
    pJob->pfnRow   = 0;
    pJob->ptFont   = 0;
    pJob->pcText   = 0;
    pJob->acText[0] = '\0';
    return pJob;
}

/** @brief 完成当前绘制项，并将它计入本帧 */
static void DrawItemCommit(void)
{
    u8DrawCount++;
}
#endif /* ST7789V_USE_DMA */

/**
 * @brief  把一块纯色矩形写入当前帧绘制列表
 * @param[in] x,y       矩形左上角坐标
 * @param[in] w,h       矩形宽高（像素）
 * @param[in] u16Color  填充色（原始 RGB565）
 * @retval 0 已写入；1 参数越界、正在发送或绘制列表已满
 */
uint8_t DevSt7789vFillRectStart(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t u16Color)
{
    if (RectCheck(x, y, w, h) != 0u)
    {
        return 1u;
    }

#if (ST7789V_USE_DMA == 0)
    /* 阻塞方式：设好窗口后逐像素发，发完才返回 */
    {
        uint32_t u32Index;
        uint32_t u32Total = (uint32_t)w * h;

        St7789vSetAddress(x, y, (uint16_t)(x + w - 1u), (uint16_t)(y + h - 1u));
        BspSpiDc(ST7789V_DC_DATA);

        for (u32Index = 0u; u32Index < u32Total; u32Index++)
        {
            St7789vSendData2Bytes(u16Color);
        }
    }
    return 0u;
#else
    {
        tSt7789vJobDef *pJob = DrawItemAlloc(x, y, w, h);

        if (pJob == 0)
        {
            return 1u;
        }

        pJob->u16Color = u16Color;
        pJob->pfnRow   = 0;
        DrawItemCommit();          /* 填完后计入本帧 */
    }
    return 0u;
#endif
}

/**
 * @brief  把一块带内容的矩形写入当前帧绘制列表，每行内容由回调给
 * @param[in] x,y,w,h  矩形位置与大小
 * @param[in] pfnRow   行内容回调，填 u16W 个原始 RGB565 像素
 * @retval 0 已写入；1 参数无效、正在发送或绘制列表已满
 */
uint8_t DevSt7789vBlitRectStart(uint16_t x, uint16_t y, uint16_t w, uint16_t h, DevSt7789vRowFn pfnRow)
{
    if (pfnRow == 0)
    {
        return 1u;
    }

    if (RectCheck(x, y, w, h) != 0u)
    {
        return 1u;
    }

#if (ST7789V_USE_DMA == 0)
    /* 阻塞方式：设窗口、逐行取内容、逐像素发 */
    {
        uint16_t u16Row;
        uint16_t i;

        St7789vSetAddress(x, y, (uint16_t)(x + w - 1u), (uint16_t)(y + h - 1u));
        BspSpiDc(ST7789V_DC_DATA);

        for (u16Row = 0u; u16Row < h; u16Row++)
        {
            pfnRow(u16Row, w, aLine);
            for (i = 0u; i < w; i++)
            {
                St7789vSendData2Bytes(aLine[i]);
            }
        }
    }
    return 0u;
#else
    {
        tSt7789vJobDef *pJob = DrawItemAlloc(x, y, w, h);

        if (pJob == 0)
        {
            return 1u;
        }

        pJob->pfnRow = pfnRow;
        DrawItemCommit();          /* 填完后计入本帧 */
    }
    return 0u;
#endif
}

/**
 * @brief  文本绘制的行内容回调：把落在第 u16Row 行的所有字形片段合成进 pu16Line
 * @note   先整行铺背景，再把每个字形的对应行盖上去；不在字库里的字符跳过。
 */
static void TextRowFn(uint16_t u16Row, uint16_t u16W, uint16_t *pu16Line)
{
    const tFontGlyph *ptGlyph;
    const char *pc;
    int16_t i16PenX;
    int16_t i16AbsY;
    int16_t i16GlyphY;
    int16_t i16Col;
    uint16_t i;

    for (i = 0u; i < u16W; i++)                /* 1) 整行先铺背景 */
    {
        pu16Line[i] = u16TextBg;
    }

    i16AbsY = (int16_t)(i16TextY0 + (int16_t)u16Row);
    i16PenX = i16TextPenX;

    for (pc = pcTextStr; *pc != '\0'; pc++)    /* 2) 逐字符，看谁的墨迹落在这一行 */
    {
        ptGlyph = FontFindGlyph(ptTextFont, (uint8_t)*pc);
        if (ptGlyph == 0)
        {
            continue;
        }

        if ((ptGlyph->width != 0u) && (ptGlyph->height != 0u))
        {
            i16GlyphY = (int16_t)(i16TextBaseY + ptGlyph->y_offset);
            if ((i16AbsY >= i16GlyphY) && (i16AbsY < (int16_t)(i16GlyphY + (int16_t)ptGlyph->height)))
            {
                /* 目标列一定落在矩形内：包围盒是按同一批字形算出来的 */
                i16Col = (int16_t)((i16PenX + ptGlyph->x_offset) - i16TextX0);
                (void)FontRenderRow(ptTextFont, ptGlyph, (uint16_t)(i16AbsY - i16GlyphY),
                                    u16TextFg, u16TextBg,
                                    &pu16Line[i16Col], (uint16_t)(u16W - (uint16_t)i16Col));
            }
        }

        i16PenX = (int16_t)(i16PenX + ptGlyph->advance);
    }
}

/**
 * @brief  把一段文本写入当前帧绘制列表
 * @param[in] i16X          笔位置横坐标
 * @param[in] i16BaselineY  基线纵坐标（不是顶边）
 * @param[in] ptFont        字库
 * @param[in] u16Fg         前景色（原始 RGB565）
 * @param[in] u16Bg         背景色（原始 RGB565），不透明绘制
 * @param[in] pcText        以 '\0' 结尾的字符串；不在字库中的字符跳过
 * @retval 0 已写入；1 无墨迹、越界、文本过长、正在发送或列表已满
 * @note   DMA 模式会复制文本，调用方可在返回后立即复用原字符串。
 *         写完本帧所有内容后调用 DevSt7789vShow() 开始异步发送。
 */
uint8_t DevSt7789vDrawText(int16_t i16X, int16_t i16BaselineY, const tFont *ptFont,
                           uint16_t u16Fg, uint16_t u16Bg, const char *pcText)
{
    const tFontGlyph *ptGlyph;          /* 当前字符在字库中查到的字形，0 = 该字符不在字库中 */
    const char *pc;                     /* 遍历 pcText 的游标 */
    int16_t i16PenX;                    /* 笔位置横坐标，画完一个字符按 advance 前移 */
    int16_t i16L;                       /* 单个字形墨迹包围盒：左边界 */
    int16_t i16T;                       /* 单个字形墨迹包围盒：上边界 */
    int16_t i16R;                       /* 单个字形墨迹包围盒：右边界（不含，即 x 取不到该列） */
    int16_t i16B;                       /* 单个字形墨迹包围盒：下边界（不含，即 y 取不到该行） */
    int16_t i16MinX = 0;                /* 整串墨迹总包围盒：最小横坐标，即刷新区左边界 */
    int16_t i16MaxX = 0;                /* 整串墨迹总包围盒：最大横坐标，即刷新区右边界 */
    int16_t i16MinY = 0;                /* 整串墨迹总包围盒：最小纵坐标，即刷新区上边界 */
    int16_t i16MaxY = 0;                /* 整串墨迹总包围盒：最大纵坐标，即刷新区下边界 */
    uint8_t u8Any = 0u;                 /* 是否已累计到带墨迹的字形，0 = 目前还没有 */
#if (ST7789V_USE_DMA != 0)
    uint8_t u8TextIndex;
#endif

    if ((ptFont == 0) || (pcText == 0))
    {
        return 1u;
    }

#if (ST7789V_USE_DMA != 0)
    for (pc = pcText, u8TextIndex = 0u; *pc != '\0'; pc++, u8TextIndex++)
    {
        if (u8TextIndex >= (ST7789V_TEXT_LEN - 1u))
        {
            return 1u;                         /* 文本副本缓冲不足，不截断显示 */
        }
    }
#endif

    /* 1) 先走一遍，算出所有字形墨迹的包围盒 */
    i16PenX = i16X;
    for (pc = pcText; *pc != '\0'; pc++)
    {
        ptGlyph = FontFindGlyph(ptFont, (uint8_t)*pc);
        if (ptGlyph == 0)
        {
            continue;
        }

        if ((ptGlyph->width != 0u) && (ptGlyph->height != 0u))
        {
            i16L = (int16_t)(i16PenX + ptGlyph->x_offset);
            i16T = (int16_t)(i16BaselineY + ptGlyph->y_offset);
            i16R = (int16_t)(i16L + (int16_t)ptGlyph->width);
            i16B = (int16_t)(i16T + (int16_t)ptGlyph->height);

            if (u8Any == 0u)
            {
                i16MinX = i16L;
                i16MaxX = i16R;
                i16MinY = i16T;
                i16MaxY = i16B;
                u8Any = 1u;
            }
            else
            {
                if (i16L < i16MinX) { i16MinX = i16L; }
                if (i16R > i16MaxX) { i16MaxX = i16R; }
                if (i16T < i16MinY) { i16MinY = i16T; }
                if (i16B > i16MaxY) { i16MaxY = i16B; }
            }
        }

        i16PenX = (int16_t)(i16PenX + ptGlyph->advance);
    }

    /* 把文本逻辑起点和前进终点纳入刷新区域，清除窄字形两侧的旧像素。
       例如首字符 '1' 的 x_offset 为 3，仅刷新墨迹会留下前一帧的竖线。 */
    if (u8Any != 0u)
    {
        if (i16X < i16MinX)
        {
            i16MinX = i16X;
        }
        if (i16PenX > i16MaxX)
        {
            i16MaxX = i16PenX;
        }
    }

    if (u8Any == 0u)      /* 整条串都没有墨迹，没什么可画 */
    {
        return 1u;
    }
    if ((i16MinX < 0) || (i16MinY < 0))   /* 不裁剪：越界就拒绝 */
    {
        return 1u;
    }

    /* 2) 把这条文本的参数连同矩形一起入队；TextRowFn 每行开始时再取出来用 */
#if (ST7789V_USE_DMA != 0)
    {
        tSt7789vJobDef *pJob = DrawItemAlloc((uint16_t)i16MinX, (uint16_t)i16MinY,
                                        (uint16_t)(i16MaxX - i16MinX),
                                        (uint16_t)(i16MaxY - i16MinY));

        if (pJob == 0)
        {
            return 1u;
        }

        for (u8TextIndex = 0u; u8TextIndex < (ST7789V_TEXT_LEN - 1u); u8TextIndex++)
        {
            pJob->acText[u8TextIndex] = pcText[u8TextIndex];
            if (pcText[u8TextIndex] == '\0')
            {
                break;
            }
        }
        pJob->acText[ST7789V_TEXT_LEN - 1u] = '\0';

        pJob->pfnRow   = TextRowFn;
        pJob->ptFont   = ptFont;
        pJob->pcText   = pJob->acText;
        pJob->u16Fg    = u16Fg;
        pJob->u16Bg    = u16Bg;
        pJob->i16PenX  = i16X;
        pJob->i16BaseY = i16BaselineY;
        pJob->i16X0    = i16MinX;
        pJob->i16Y0    = i16MinY;
        DrawItemCommit();          /* 填完后计入本帧 */
    }
    return 0u;
#else
    ptTextFont   = ptFont;
    pcTextStr    = pcText;
    u16TextFg    = u16Fg;
    u16TextBg    = u16Bg;
    i16TextPenX  = i16X;
    i16TextBaseY = i16BaselineY;
    i16TextX0    = i16MinX;
    i16TextY0    = i16MinY;

    return DevSt7789vBlitRectStart((uint16_t)i16MinX, (uint16_t)i16MinY,
                                   (uint16_t)(i16MaxX - i16MinX),
                                   (uint16_t)(i16MaxY - i16MinY), TextRowFn);
#endif
}

/**
 * @brief  整屏纯色填充（FillRectStart 的整屏特例）
 */
uint8_t DevSt7789vFillScreenStart(uint16_t u16Color)
{
    return DevSt7789vFillRectStart(0u, 0u, ST7789V_WIDTH, ST7789V_HEIGHT, u16Color);
}

/**
 * @brief  提交当前绘制列表并启动异步发送
 * @retval 0 已启动；1 正在发送或当前列表为空
 */
uint8_t DevSt7789vShow(void)
{
#if (ST7789V_USE_DMA == 0)
    return 0u;
#else
    if ((u8Showing != 0u) || (u8DrawCount == 0u))
    {
        return 1u;
    }

    u8SendIndex = 0u;
    u8Showing = 1u;
    return 0u;
#endif
}

void DevSt7789vService(void)
{
#if (ST7789V_USE_DMA == 0)
    /* 阻塞方式下没有待推进的矩形 */
#else
    tSt7789vJobDef *pJob;
    uint16_t i;

    BspSpiDmaService();                          /* 先让 BSP 收尾上一行 */

    if (u8Showing == 0u)/* Show 尚未提交 */
    {
        return;
    } 
    if (BspSpiDmaIsIdle() == 0u) /* 上一行还在搬 */
    {
        return;
    }

    if (u8SendIndex >= u8DrawCount)              /* 整帧发送完成 */
    {
        u8Showing = 0u;
        u8SendIndex = 0u;
        u8DrawCount = 0u;
        return;
    }

    pJob = &tDrawItems[u8SendIndex];

    if (pJob->u16Left == 0u)                     /* 当前绘制项发送完成 */
    {
        u8SendIndex++;
        return;                                  /* 下一趟处理下一项 */
    }

    if (pJob->u16Row == 0u)                      /* 这一块的第一行：先把窗口设好 */
    {
        St7789vSetAddress(pJob->u16X, pJob->u16Y,
                          (uint16_t)(pJob->u16X + pJob->u16W - 1u),
                          (uint16_t)(pJob->u16Y + pJob->u16Left - 1u));
        BspSpiDc(ST7789V_DC_DATA);
    }

    if (pJob->pfnRow != 0)                       /* 内容由回调给 */
    {
        /* 这一行的文本参数：从当前这块搬到 TextRowFn 读的那组量 */
        ptTextFont   = pJob->ptFont;
        pcTextStr    = pJob->pcText;
        u16TextFg    = pJob->u16Fg;
        u16TextBg    = pJob->u16Bg;
        i16TextPenX  = pJob->i16PenX;
        i16TextBaseY = pJob->i16BaseY;
        i16TextX0    = pJob->i16X0;
        i16TextY0    = pJob->i16Y0;

        pJob->pfnRow(pJob->u16Row, pJob->u16W, aLine);

        for (i = 0u; i < pJob->u16W; i++)
        {
            aLine[i] = U16_SWAP_BYTES(aLine[i]);   /* 就地转成屏要的字节序 */
        }
    }
    else                                         /* 纯色：直接填 */
    {
        for (i = 0u; i < pJob->u16W; i++)
        {
            aLine[i] = U16_SWAP_BYTES(pJob->u16Color);
        }
    }

    if (BspSpiSendDmaStart((const uint8_t *)aLine, (uint16_t)(pJob->u16W * 2u)) == 0u)
    {
        pJob->u16Row++;
        pJob->u16Left--;                         /* 启动成功才算发出去一行 */
    }
#endif /* ST7789V_USE_DMA */
}

uint16_t DevSt7789vTask(void)
{
    PT_BEGIN()
    while (1)
    {
        PT_WAIT_UNTIL(0u);          /* 每个主循环都跑一趟，不能用毫秒节拍 */
        DevSt7789vService();
    }
    PT_END()
}