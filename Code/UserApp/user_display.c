#include "user_display.h"

#include "Components/color/color.h"
#include "UserDev/ws2812/dev_ws2812.h"
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

void DisplayPowerOn(void)
{
    // DevWs2812Fill(0,0,0);
    ColorHsvToRgb(0, 255, 20, &u8Red, &u8Green, &u8Blue);
    DevWs2812SetPixel(0,u8Red,u8Green,u8Blue);
}

static void DisplayOff(void)
{
    // DevWs2812Fill(0,0,0);
    ColorHsvToRgb(120, 255, 20, &u8Red, &u8Green, &u8Blue);
    DevWs2812SetPixel(1,u8Red,u8Green,u8Blue);
}

void DisplayRun(void)
{
    // DevWs2812Fill(0,0,0);
    ColorHsvToRgb(240, 255, 20, &u8Red, &u8Green, &u8Blue);
    DevWs2812SetPixel(2,u8Red,u8Green,u8Blue);
}


static void Ws2812Control(void)
{

    ColorHsvToRgb(0, 255, 20, &u8Red, &u8Green, &u8Blue);
    DevWs2812SetPixel(0,u8Red,u8Green,u8Blue);

    ColorHsvToRgb(120, 255, 20, &u8Red, &u8Green, &u8Blue);
    DevWs2812SetPixel(1,u8Red,u8Green,u8Blue);

    ColorHsvToRgb(240, 255, 20, &u8Red, &u8Green, &u8Blue);
    DevWs2812SetPixel(2,u8Red,u8Green,u8Blue);

    ColorHsvToRgb(360, 0, 20, &u8Red, &u8Green, &u8Blue);
    DevWs2812SetPixel(3,u8Red,u8Green,u8Blue);

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


        DevWs2812Flush();

    }
    PT_END();
}