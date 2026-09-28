#ifndef __DEV_SENSOR_H__
#define __DEV_SENSOR_H__

#include "user_global.h"

#define SNESOR_TASK_MS (10)

typedef enum
{
    E_DEV_VBUS,       /**< 输入母线电压（PC0 / IN10） */
    E_DEV_VOUT,       /**< 输出电压（PC3 / IN13）     */
    E_DEV_IBUS,         /**< 输出电流（PA3 / IN3）      */
    E_DEV_POW,
    E_DEV_CH_MAX      /**< 通道总数（边界标记）       */
} eDevChannelDef;
float DevSensorGetValue(eDevChannelDef chx);
uint16_t DevSensorTask(void);

#endif


