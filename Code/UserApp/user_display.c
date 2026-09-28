#include "user_display.h"

#include "Components/color/color.h"
#include "UserDev/ws2812/dev_ws2812.h"
#include "UserDev/st7789v/dev_st7789v.h"
#include "UserDev/sensor/dev_sensor.h"

#include "UserApp/user_system.h"
#include "Components/font/font_inter_28.h"

#define MID_OFFSET (10)

typedef struct sDisModeTableDef
{
    eSystemStateDef eState;
    FuncPtr pfDisplay;
} sDisModeTableDef;


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

static void DisplayOff(void)
{
    BspGpioSetLed(BspGpioReadVoutFg());
    ColorHsvToRgb(DISPLAY_HUE_MAGENTA, 255, 20, &u8Red, &u8Green, &u8Blue);
    DevWs2812Fill(u8Red, u8Green, u8Blue);

    /* 先写入本帧绘制列表，最后由 Show 一次提交并异步发送 */
    
    DevSt7789vFillRectStart(0, 0,                   ST7789V_WIDTH, ST7789V_HEIGHT/3, COLOR_RED);
    DevSt7789vFillRectStart(0, ST7789V_HEIGHT/3,    ST7789V_WIDTH, ST7789V_HEIGHT/3, COLOR_GREEN);
    DevSt7789vFillRectStart(0, ST7789V_HEIGHT/3*2,  ST7789V_WIDTH, ST7789V_HEIGHT/3, COLOR_BLUE);
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
    // sprintf(string,"%0.2f",i+=0.01);
    ColorHsvToRgb(u16colcor++, 255, 20, &u8Red, &u8Green, &u8Blue);
    DevWs2812SetPixel(0, u8Red, u8Green, u8Blue );

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