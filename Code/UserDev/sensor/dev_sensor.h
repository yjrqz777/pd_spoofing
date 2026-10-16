/**
 * @file dev_sensor.h
 * @brief 传感器设备层接口与实现。
 * @author 段志炫
 * @date 2026-10-15
 * @version 0.1.1
 * @par 修改记录
 * 2026-10-15 按项目规范补充中文 Doxygen 注释。
 */
#ifndef __DEV_SENSOR_H__
#define __DEV_SENSOR_H__

#include "user_global.h"

#define SNESOR_TASK_MS (10)

typedef enum
{
    E_DEV_VBUS,       /* 输入母线电压（PC0 / IN10） */
    E_DEV_VOUT,       /* 输出电压（PC3 / IN13） */
    E_DEV_IBUS,         /* 输出电流（PA3 / IN3） */
    E_DEV_POW,
    E_DEV_CH_MAX      /* 通道总数（边界标记） */
} eDevChannelDef;
/**
 * @brief DevSensorGetValue 接口处理函数。
 * @param[in] chx 参数值，单位和范围按接口定义。
 * @return 返回状态或计算结果，具体含义见返回值定义。
 * @note 调用时应遵循模块初始化顺序与运行上下文要求。
 */
float DevSensorGetValue(eDevChannelDef chx);
/**
 * @brief DevSensorTask 接口处理函数。
无输入参数。
 * @return 返回状态或计算结果，具体含义见返回值定义。
 * @note 调用时应遵循模块初始化顺序与运行上下文要求。
 */
uint16_t DevSensorTask(void);

#endif


