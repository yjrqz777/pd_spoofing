#include "user_display.h"

#include "Components/color/color.h"
#include "UserDev/ws2812/dev_ws2812.h"
#include "UserDev/st7789v/dev_st7789v.h"

#include "UserApp/user_system.h"



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

    if (++timeCount >= 60 / DISPLAY_TASK_MS)
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
    ColorHsvToRgb(DISPLAY_HUE_RED, 255, 20, &u8Red, &u8Green, &u8Blue);
    DevWs2812Fill(u8Red,u8Green,u8Blue);
}

/* ---------------- 运动方块演示 ----------------
 * 10×10 的方块每步挪一格。因为一步只走 1px，新旧位置有 9px 重叠，
 * 所以不重画整块，只动两条边：擦掉"离开的那一列"，画出"进入的那一列"。
 * 设备层一次只接一个矩形，两条边分两拍提交；中间那 9 列一直不动，交替期间看不出闪。 */
#define DEMO_BLOCK_W    (20u)
#define DEMO_BLOCK_H    (20u)
#define DEMO_BLOCK_FG   (ST7789V_RED)
#define DEMO_BLOCK_BG   (ST7789V_GRAY)   /* 必须与 DevSt7789vInit 末尾的整屏底色一致 */

static uint16_t sDemoX = 0u;             /* 方块当前左上角 */
static uint16_t sDemoY = 0u;
static uint16_t sDemoNextX = 0u;         /* 下一步落点 */
static uint16_t sDemoNextY = 0u;
static uint8_t  sDemoJump = 0u;          /* 1 = 下一步换行，整块搬 */
static uint8_t  sDemoPhase = 0u;         /* 0 = 待擦边，1 = 待画边 */
static uint8_t  sDemoDrawn = 0u;         /* 0 = 方块还没画出来 */

/* 规划下一步：右边还放得下就往右挪一格，否则换到下一行；到底回到第一行 */
static void DemoPlanNext(void)
{
    if ((uint16_t)(sDemoX + DEMO_BLOCK_W) < ST7789V_WIDTH)
    {
        sDemoNextX = (uint16_t)(sDemoX + 1u);
        sDemoNextY = sDemoY;
        sDemoJump  = 0u;
    }
    else
    {
        sDemoNextX = 0u;
        sDemoNextY = ((uint16_t)(sDemoY + (DEMO_BLOCK_H * 2u)) <= ST7789V_HEIGHT)
                     ? (uint16_t)(sDemoY + DEMO_BLOCK_H) : 0u;
        sDemoJump  = 1u;
    }
}

void DisplayRun(void)
{
    static uint16_t timeCount = 0;
    static uint16_t u16colcor = 0;
    uint8_t u8Ok;
    ColorHsvToRgb(u16colcor, 255, 20, &u8Red, &u8Green, &u8Blue);
    DevWs2812SetPixel(0, u8Red, u8Green, u8Blue);


    u16colcor   = (uint8_t)(rand() % COLOR_HUE_MAX);


    if (sDemoDrawn == 0u)                       /* 第一步：先把方块整个画出来 */
    {
        if (DevSt7789vFillRectStart(sDemoX, sDemoY, DEMO_BLOCK_W, DEMO_BLOCK_H, DEMO_BLOCK_FG) == 0u)
        {
            sDemoDrawn = 1u;
            DemoPlanNext();
        }
        return;
    }

    /* 每拍只提交一个矩形；返回 1（设备层还忙或越界）就下一拍重试 */
    if (sDemoPhase == 0u)                       /* 擦掉离开的部分 */
    {
        if (sDemoJump != 0u)
        {
            u8Ok = (DevSt7789vFillRectStart(sDemoX, sDemoY, DEMO_BLOCK_W, DEMO_BLOCK_H, DEMO_BLOCK_BG) == 0u);
        }
        else
        {
            u8Ok = (DevSt7789vFillRectStart(sDemoX, sDemoY, 1u, DEMO_BLOCK_H, DEMO_BLOCK_BG) == 0u);
        }

        if (u8Ok != 0u)
        {
            sDemoPhase = 1u;
        }
    }
    else                                        /* 画出进入的部分 */
    {
        if (sDemoJump != 0u)
        {
            u8Ok = (DevSt7789vFillRectStart(sDemoNextX, sDemoNextY, DEMO_BLOCK_W, DEMO_BLOCK_H, DEMO_BLOCK_FG) == 0u);
        }
        else
        {
            u8Ok = (DevSt7789vFillRectStart((uint16_t)(sDemoNextX + DEMO_BLOCK_W - 1u), sDemoNextY,
                                            1u, DEMO_BLOCK_H, DEMO_BLOCK_FG) == 0u);
        }

        if (u8Ok != 0u)
        {
            sDemoX = sDemoNextX;
            sDemoY = sDemoNextY;
            sDemoPhase = 0u;
            DemoPlanNext();
        }
    }
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

        DevWs2812Flush();

    }
    PT_END();
}