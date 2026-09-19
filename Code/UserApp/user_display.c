#include "user_display.h"





static void Ws2812Contorl(void)
{
    DevWs2812SetPixel(0,255,0,0);
    DevWs2812SetPixel(1,0,255,0);
    DevWs2812SetPixel(2,0,0,255);
    DevWs2812SetPixel(3,255,255,255);
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