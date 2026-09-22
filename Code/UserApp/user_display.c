#include "user_display.h"

#include "Components/color/color.h"
#include "UserDev/ws2812/dev_ws2812.h"





static void Ws2812Contorl(void)
{
    static uint8_t u8Red, u8Green, u8Blue;
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
        Ws2812Contorl();


        DevWs2812Flush();

    }
    PT_END();
}