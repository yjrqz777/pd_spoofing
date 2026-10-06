#include "user_display.h"

#include "Components/color/color.h"
#include "UserDev/ws2812/dev_ws2812.h"
#include "UserDev/st7789v/dev_st7789v.h"
#include "UserDev/sensor/dev_sensor.h"

#include "UserApp/user_system.h"
#include "UserApp/user_pd.h"
#include "Components/font/font_inter_28.h"
#include "Components/font/img_xj_bw.h"

#define MID_OFFSET (10)

typedef struct sDisModeTableDef
{
    eSystemStateDef eState;
    FuncPtr pfDisplay;
} sDisModeTableDef;



static uint8_t u8RunFg = 0;
static void DisplayPowerOn(void);
static void DisplayOff(void);
static void DisplayRun(void);
static uint8_t u8Red, u8Green, u8Blue;

sDisModeTableDef sDisModeTable[E_SYSTEM_MAX] =
{
    {E_SYSTEM_POWERON, DisplayPowerOn},
    {E_SYSTEM_OFF,     DisplayOff},
    {E_SYSTEM_RUN,     DisplayRun},
};




/* 把那段逐灯点亮的逻辑抽成一个函数，DisplayPowerOn 只负责调用 */
static void DisplayLightUpOneByOne(void)
{
    static uint16_t timeCount = 0;
    static uint8_t u8i = 1;                 /* 当前已点亮的灯数：1 到 4 */
    uint8_t index;

    ColorHsvToRgb(DISPLAY_HUE_RED, 255, 20, &u8Red, &u8Green, &u8Blue);

    if (++timeCount >= 250 / DISPLAY_TASK_MS)
    {
        timeCount = 0;
        if (u8i < 4)                        /* 每 250ms 多点亮一个，全部点亮后保持 */
        {
            u8i++;
        }
    }

    /* 帧缓冲每拍都被清空重画，所以每个周期都要把当前应亮的灯重新写一遍 */
    index = (uint8_t)((_0000_0001 << u8i) - 1);
    DevWs2812SetMask(index, u8Red, u8Green, u8Blue);
}



/* 四灯流水：点从第 0 个流到第 3 个，每流完一圈从第 3 个往回锁存一个，四个全亮后保持 */
void DisplayFlow(void)
{
    static uint16_t timeCount = 0;
    static uint8_t u8i = 0;                 /* 点当前所在的灯：0 到 3 */
    static uint8_t u8LatchNum = 0;          /* 已经锁存的灯数：从第 3 个往回数 */
    uint8_t u8Top;                          /* 这一圈点能走到的最高位 */
    uint8_t index;

    ColorHsvToRgb(DISPLAY_HUE_GREEN, 255, 20, &u8Red, &u8Green, &u8Blue);

    if (++timeCount >= 90 / DISPLAY_TASK_MS)
    {
        timeCount = 0;
        if (u8LatchNum < 4)
        {
            u8Top = (uint8_t)(3 - u8LatchNum);      /* 上面已经锁存的灯不用再走 */
            if (u8i >= u8Top)                       /* 走到这一圈的顶：把它锁存，点回到第 0 个 */
            {
                u8i = 0;
                u8LatchNum++;
            }
            else
            {
                u8i++;
            }
        }
    }

    /* 帧缓冲每拍都被清空重画，所以每个周期都要把当前应亮的灯重新写一遍 */
    index = (uint8_t)(_0000_0001 << u8i);
    index |= (uint8_t)(((_0000_0001 << u8LatchNum) - 1) << (4 - u8LatchNum));   /* 已锁存的灯，从第 3 个往回 */
    DevWs2812SetMask(index, u8Red, u8Green, u8Blue);
}


void DisplayPowerOn(void)
{
    DisplayFlow();
}

/* 三色横带里文字的位置：每块高 = 屏高/3，28px 字形的墨迹高出基线 21 行，
   于是基线 = 块顶 + 块高/2 + 21/2，文字在块里竖直居中。 */
#define BAND_H            (ST7789V_HEIGHT/3)
#define BAND_BASELINE(k)  (BAND_H*(k) + BAND_H/2 + 10)

/**
 * @brief  重刷三条底色横带（上红、中绿、下蓝）
 * @retval 0 三块都排进了当前帧；1 没排上，需要下一拍重试
 * @note   FillRectStart 在显示忙的时候直接返回 1，一块都排不进帧。所以调用方
 *         必须看返回值再翻"底色已刷"的标志：否则这次重刷被丢掉、标志却翻了，
 *         底色再也刷不回来，上一页的字（例如 INIT OK）就留在屏上。
 */
static uint8_t BandsRepaint(void)
{
    if (DevSt7789vFillRectStart(0, 0,            ST7789V_WIDTH, BAND_H, COLOR_RED)   != 0u)
    {
        return 1u;
    }
    if (DevSt7789vFillRectStart(0, BAND_H,       ST7789V_WIDTH, BAND_H, COLOR_GREEN) != 0u)
    {
        return 1u;
    }
    if (DevSt7789vFillRectStart(0, BAND_H * 2u,  ST7789V_WIDTH, BAND_H, COLOR_BLUE)  != 0u)
    {
        return 1u;
    }

    return 0u;
}

static void DisplayOff(void)
{
    char        acString[9];
    const char *pcPps;
    uint16_t    u16TextW;

    BspGpioSetLed(BspGpioReadVoutFg());
    ColorHsvToRgb(DISPLAY_HUE_MAGENTA, 255, 20, &u8Red, &u8Green, &u8Blue);
    DevWs2812Fill(u8Red, u8Green, u8Blue);

    pcPps = (UserPdHasPps() != 0u) ? "PPS YES" : "PPS NO";
    sprintf(acString, "%0.2fV", UserPdGetTargetMv() / 1000.0f);


    if (u8RunFg == 0)
    {
        log_info("DisplayOff: run fg=%d", u8RunFg);
        /* 进这一页先重刷底色（上红中绿下蓝）：刷上了才翻标志，下一拍再写字 */
        if (BandsRepaint() == 0u)
        {
            u8RunFg = 1;
        }
        return;
    }
    




    /* 红色块：初始化成功 */
    u16TextW = FontMeasureText(&gtFontInter28, "INIT OK");
    (void)DevSt7789vDrawText((int16_t)((ST7789V_WIDTH - u16TextW) / 2u), (int16_t)BAND_BASELINE(0),
                             &gtFontInter28, COLOR_WHITE, COLOR_RED, "INIT OK");

    /* 绿色块：对端是否支持可调档（PPS） */
    u16TextW = FontMeasureText(&gtFontInter28, pcPps);
    (void)DevSt7789vDrawText((int16_t)((ST7789V_WIDTH - u16TextW) / 2u), (int16_t)BAND_BASELINE(1),
                             &gtFontInter28, COLOR_WHITE, COLOR_GREEN, pcPps);

    /* 蓝色块：当前档位电压 */
    u16TextW = FontMeasureText(&gtFontInter28, acString);
    (void)DevSt7789vDrawText((int16_t)((ST7789V_WIDTH - u16TextW) / 2u), (int16_t)BAND_BASELINE(2),
                             &gtFontInter28, COLOR_WHITE, COLOR_BLUE, acString);

    // DevSt7789vFillRectStart(ST7789V_WIDTH/2, 0,     1, ST7789V_HEIGHT, COLOR_WHITE);
}



char *StrAddUnit(char *p, const char *unit, uint8_t len)
{
    char *end = p + strlen(p);   // 跳过原有字符串
    memcpy(end, unit, len);      // 按 len 追加单位字符
    end[len] = '\0';             // 补字符串结束符
    return p;
}



void DisplayRun(void)
{
    static char string[4][9];
    static uint16_t u16colcor = 0;

    if (u8RunFg == 1)
    {
        log_info("DisplayRun: run fg=%d", u8RunFg);
        /* 从关机页切过来：先重刷底色，上一页的字才会被盖掉 */
        if (BandsRepaint() == 0u)
        {
            u8RunFg = 0;
        }
        return;
    }


    
    // sprintf(string,"%0.2f",i+=0.01);
    // ColorHsvToRgb(u16colcor++, 255, 20, &u8Red, &u8Green, &u8Blue);
    // DevWs2812SetPixel(0, u8Red, u8Green, u8Blue );

    sprintf(string[0],"%0.2f",DevSensorGetValue(E_DEV_VBUS));
    sprintf(string[1],"%0.2f",DevSensorGetValue(E_DEV_VOUT));
    sprintf(string[2],"%0.2f",DevSensorGetValue(E_DEV_IBUS));
    sprintf(string[3],"%0.2f",DevSensorGetValue(E_DEV_POW));
    (void)DevSt7789vDrawText(0,                 ST7789V_HEIGHT/3-MID_OFFSET,   &gtFontInter28, COLOR_WHITE, COLOR_RED,   StrAddUnit(string[0],"V",2));
    (void)DevSt7789vDrawText(ST7789V_WIDTH/2,   ST7789V_HEIGHT/3-MID_OFFSET,   &gtFontInter28, COLOR_WHITE, COLOR_RED,   StrAddUnit(string[1],"V",2));
    (void)DevSt7789vDrawText(0,                 ST7789V_HEIGHT/3*2-MID_OFFSET, &gtFontInter28, COLOR_WHITE, COLOR_GREEN, StrAddUnit(string[2],"A",2));
    (void)DevSt7789vDrawText(0,                 ST7789V_HEIGHT-MID_OFFSET,     &gtFontInter28, COLOR_WHITE, COLOR_BLUE,  StrAddUnit(string[3],"W",2));


    BspGpioSetLed(BspGpioReadVoutFg());



}




uint16_t UserDisplayTask(void)
{
    PT_BEGIN()
    {
        DevWs2812Init();
    }
    while (1)
    {
        PT_WAIT_UNTIL(DISPLAY_TASK_MS / OS_TICK_MS);
        // Ws2812Contorl();
        DevWs2812Fill(0,0,0);
        sDisModeTable[tSysData.eState].pfDisplay();
        // DisplayRun();

        (void)DevSt7789vShow();
        DevWs2812Flush();

    }
    PT_END();
}