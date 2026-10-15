/**
 * @file dev_sensor.c
 * @brief 传感器设备层接口与实现。
 * @author 段志炫
 * @date 2026-10-15
 * @version 0.1.1
 * @par 修改记录
 * 2026-10-15 按项目规范补充中文 Doxygen 注释。
 */
#include "dev_sensor.h"
#include "UserBsp/bsp_adc.h"

static float Vbus, Vout, Ibus, Pow;



/**
 * @brief DevSensorConvertVbus 接口处理函数。
无输入参数。
 * @return 返回状态或计算结果，具体含义见返回值定义。
 * @note 调用时应遵循模块初始化顺序与运行上下文要求。
 */
float DevSensorConvertVbus(void)
{
    return BspAdcGetRaw(E_BSP_ADC_VBUS) * BOARD_VOUT_CODE_TO_V;
}

/**
 * @brief DevSensorConvertVout 接口处理函数。
无输入参数。
 * @return 返回状态或计算结果，具体含义见返回值定义。
 * @note 调用时应遵循模块初始化顺序与运行上下文要求。
 */
float DevSensorConvertVout(void)
{
    return BspAdcGetRaw(E_BSP_ADC_VOUT) * BOARD_VBUS_CODE_TO_V;
}

/**
 * @brief DevSensorConvertIbus 接口处理函数。
无输入参数。
 * @return 返回状态或计算结果，具体含义见返回值定义。
 * @note 调用时应遵循模块初始化顺序与运行上下文要求。
 */
float DevSensorConvertIbus(void)
{
    return BspAdcGetRaw(E_BSP_ADC_IBUS) * BOARD_IBUS_CODE_TO_A;
}


/**
 * @brief DevSensorGetValue 接口处理函数。
 * @param[in] chx 参数值，单位和范围按接口定义。
 * @return 返回状态或计算结果，具体含义见返回值定义。
 * @note 调用时应遵循模块初始化顺序与运行上下文要求。
 */
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


/**
 * @brief DevSensorDrocess 接口处理函数。
无输入参数。
 * @return 无返回值。
 * @note 调用时应遵循模块初始化顺序与运行上下文要求。
 */
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



/**
 * @brief DevSensorTask 接口处理函数。
无输入参数。
 * @return 返回状态或计算结果，具体含义见返回值定义。
 * @note 调用时应遵循模块初始化顺序与运行上下文要求。
 */
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