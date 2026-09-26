

#include "dev_st7789v.h"

static void St7789vSendCmd(uint8_t cmd)
{
    BspSpiDc(CMD);
    BspSpiSendByte(cmd);
}

static void St7789vSendData(uint8_t dat)
{
    BspSpiDc(DATA);
    BspSpiSendByte(dat);
}


void St7789vSendData2Bytes(uint16_t dat)
{
    BspSpiDc(DATA);
	St7789vSendData(dat>>8);
	St7789vSendData(dat);
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
void St7789vSetAddress(uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2)
{
	if(USE_HORIZONTAL==0)
	{
		St7789vSendCmd(0x2a);        // 列地址设置
		St7789vSendData2Bytes(x1+52);
		St7789vSendData2Bytes(x2+52);
		St7789vSendCmd(0x2b);        // 行地址设置
		St7789vSendData2Bytes(y1+40);
		St7789vSendData2Bytes(y2+40);
		St7789vSendCmd(0x2c);        // 存储器写
	}
	else if(USE_HORIZONTAL==1)
	{
		St7789vSendCmd(0x2a);        // 列地址设置
		St7789vSendData2Bytes(x1+53);
		St7789vSendData2Bytes(x2+53);
		St7789vSendCmd(0x2b);        // 行地址设置
		St7789vSendData2Bytes(y1+40);
		St7789vSendData2Bytes(y2+40);
		St7789vSendCmd(0x2c);        // 存储器写
	}
	else if(USE_HORIZONTAL==2)
	{
		St7789vSendCmd(0x2a);        // 列地址设置
		St7789vSendData2Bytes(x1+40);
		St7789vSendData2Bytes(x2+40);
		St7789vSendCmd(0x2b);        // 行地址设置
		St7789vSendData2Bytes(y1+53);
		St7789vSendData2Bytes(y2+53);
		St7789vSendCmd(0x2c);        // 存储器写
	}
	else
	{
		St7789vSendCmd(0x2a);        // 列地址设置
		St7789vSendData2Bytes(x1+40);
		St7789vSendData2Bytes(x2+40);
		St7789vSendCmd(0x2b);        // 行地址设置
		St7789vSendData2Bytes(y1+52);
		St7789vSendData2Bytes(y2+52);
		St7789vSendCmd(0x2c);        // 存储器写
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
	St7789vSetAddress(0, 0, LCD_W - 1, LCD_H - 1);
	for (i = 0; i < (LCD_W) * (LCD_H); i++)
	{
		St7789vSendData2Bytes(color);
	}
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
    St7789vSetDir(USE_HORIZONTAL);
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

    DevSt7789vFillRect(RED);
	DevSt7789vDrawPoint(5, 5, BLUE);
}



// /**
//  * @brief  在指定位置画一个像素点
//  * @param[in] x      x 坐标
//  * @param[in] y      y 坐标
//  * @param[in] color  点的颜色（RGB565）
//  */
// void LCD_DrawPoint(uint16_t x,uint16_t y,uint16_t color)
// {
// 	St7789vSetAddress(x,y,x,y);//设置光标位置 
// 	St7789vSendData2Bytes(color);
// } 


// /**
//  * @brief  画线（Bresenham 算法）
//  * @param[in] x1,y1  起点坐标
//  * @param[in] x2,y2  终点坐标
//  * @param[in] color  线的颜色（RGB565）
//  */
// void LCD_DrawLine(uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2,uint16_t color)
// {
// 	uint16_t t; 
// 	int xerr=0,yerr=0,delta_x,delta_y,distance;
// 	int incx,incy,uRow,uCol;
// 	delta_x=x2-x1; //计算坐标增量 
// 	delta_y=y2-y1;
// 	uRow=x1;//画线起点坐标
// 	uCol=y1;
// 	if(delta_x>0)incx=1; //设置单步方向 
// 	else if (delta_x==0)incx=0;//垂直线 
// 	else {incx=-1;delta_x=-delta_x;}
// 	if(delta_y>0)incy=1;
// 	else if (delta_y==0)incy=0;//水平线 
// 	else {incy=-1;delta_y=-delta_y;}
// 	if(delta_x>delta_y)distance=delta_x; //选取基本增量坐标轴 
// 	else distance=delta_y;
// 	for(t=0;t<distance+1;t++)
// 	{
// 		LCD_DrawPoint(uRow,uCol,color);//画点
// 		xerr+=delta_x;
// 		yerr+=delta_y;
// 		if(xerr>distance)
// 		{
// 			xerr-=distance;
// 			uRow+=incx;
// 		}
// 		if(yerr>distance)
// 		{
// 			yerr-=distance;
// 			uCol+=incy;
// 		}
// 	}
// }


// /**
//  * @brief  画空心矩形
//  * @param[in] x1,y1  左上角坐标
//  * @param[in] x2,y2  右下角坐标
//  * @param[in] color  边框颜色（RGB565）
//  */
// void LCD_DrawRectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2,uint16_t color)
// {
// 	LCD_DrawLine(x1,y1,x2,y1,color);
// 	LCD_DrawLine(x1,y1,x1,y2,color);
// 	LCD_DrawLine(x1,y2,x2,y2,color);
// 	LCD_DrawLine(x2,y1,x2,y2,color);
// }


// /**
//  * @brief  画空心圆（Bresenham 算法）
//  * @param[in] x0,y0  圆心坐标
//  * @param[in] r      半径（像素）
//  * @param[in] color  圆的颜色（RGB565）
//  */
// void Draw_Circle(uint16_t x0,uint16_t y0,uint8_t r,uint16_t color)
// {
// 	int a,b;
// 	a=0;b=r;	  
// 	while(a<=b)
// 	{
// 		LCD_DrawPoint(x0-b,y0-a,color);             //3           
// 		LCD_DrawPoint(x0+b,y0-a,color);             //0           
// 		LCD_DrawPoint(x0-a,y0+b,color);             //1                
// 		LCD_DrawPoint(x0-a,y0-b,color);             //2             
// 		LCD_DrawPoint(x0+b,y0+a,color);             //4               
// 		LCD_DrawPoint(x0+a,y0-b,color);             //5
// 		LCD_DrawPoint(x0+a,y0+b,color);             //6 
// 		LCD_DrawPoint(x0-b,y0+a,color);             //7
// 		a++;
// 		if((a*a+b*b)>(r*r))//判断要画的点是否过远
// 		{
// 			b--;
// 		}
// 	}
// }

