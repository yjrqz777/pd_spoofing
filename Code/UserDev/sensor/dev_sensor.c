#include "dev_sensor.h"
#include "UserBsp/bsp_adc.h"

static float Vbus, Vout, Ibus, Pow;



float DevSensorConvertVbus(void)
{
    return BspAdcGetRaw(E_BSP_ADC_VBUS) * BOARD_VOUT_CODE_TO_V;
}

float DevSensorConvertVout(void)
{
    return BspAdcGetRaw(E_BSP_ADC_VOUT) * BOARD_VBUS_CODE_TO_V;
}

float DevSensorConvertIbus(void)
{
    return BspAdcGetRaw(E_BSP_ADC_IBUS) * BOARD_IBUS_CODE_TO_A;
}


float DevSensorGetValue(eDevChannelDef chx)
{
    float vlaue = 0;
    switch (chx)
    {
    case E_DEV_VBUS:
        vlaue = Vbus;
        break;
    
    case E_DEV_VOUT:
        vlaue = Vout;
        break;
    case E_DEV_IBUS:
        vlaue = Ibus;
        break;
    case E_DEV_POW:
        vlaue = Pow;
        break;
    default:
        break;
    }
    return vlaue;
}


static void DevSensorDrocess(void)
{
    tFilterEma vBusFilter, vOutFilter, iBusFilter;

    FilterEmaInit(&vBusFilter, 0.1f);       /* 初始化时调用一次 */
    FilterEmaInit(&vOutFilter, 0.1f);       /* 初始化时调用一次 */
    FilterEmaInit(&iBusFilter, 0.1f);       /* 初始化时调用一次 */

    Vbus = FilterEmaUpdate(&vBusFilter, DevSensorConvertVbus()); /* 每次采样调用 */
    Vout = FilterEmaUpdate(&vOutFilter, DevSensorConvertVout()); /* 每次采样调用 */
    Ibus = FilterEmaUpdate(&iBusFilter, DevSensorConvertIbus()); /* 每次采样调用 */

    Pow = Ibus * Vout;
}



uint16_t DevSensorTask(void)
{
    PT_BEGIN()
    {
    }
    while (1)
    {
        PT_WAIT_UNTIL(SNESOR_TASK_MS / OS_TICK_MS);
        (void)BspAdcReadRaw();
        DevSensorDrocess();
        BspAdcStartInject();
    }
    PT_END();
}