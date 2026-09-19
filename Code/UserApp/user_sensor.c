#include "user_sensor.h"




uint16_t BspSensorTask(void)
{
    PT_BEGIN()
    {
    }
    while (1)
    {
        PT_WAIT_UNTIL(SNESOR_TASK_MS / OS_TICK_MS);
        BspAdcReadRaw();
        BspAdcStartInject();
    }
    PT_END();
}
