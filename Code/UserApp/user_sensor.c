#include "user_sensor.h"

#include "UserBsp/bsp_adc.h"




uint16_t BspSensorTask(void)
{
    PT_BEGIN()
    {
    }
    while (1)
    {
        PT_WAIT_UNTIL(SNESOR_TASK_MS / OS_TICK_MS);
        (void)BspAdcReadRaw();
        BspAdcStartInject();

        // static uint8_t u8Hue = 0u;
        // if (BspWs2812IsIdle() != 0u)
        // {
        //     uint8_t au8Grb[12] = { u8Hue, 0u, (uint8_t)(255u - u8Hue),  /* 每颗同色，GRB */
        //                         u8Hue, 0u, (uint8_t)(255u - u8Hue),
        //                         u8Hue, 0u, (uint8_t)(255u - u8Hue),
        //                         u8Hue, 0u, (uint8_t)(255u - u8Hue) };
        //     (void)BspWs2812LoadBytes(au8Grb, 12u);
        //     (void)BspWs2812Show();
        //     u8Hue++;
        // }

    }
    PT_END();
}
