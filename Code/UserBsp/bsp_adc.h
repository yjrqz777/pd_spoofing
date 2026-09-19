#ifndef __BSP_ADC_H__
#define __BSP_ADC_H__

#include "user_global.h"

/** @brief 采样通道枚举 */
typedef enum
{
    E_BSP_ADC_VBUS,       /**< 输入母线电压（PC0 / IN10） */
    E_BSP_ADC_VOUT,       /**< 输出电压（PC3 / IN13）     */
    E_BSP_ADC_IBUS,   /**< 输出电流（PA3 / IN3）      */
    E_BSP_ADC_CH_MAX      /**< 通道总数（边界标记）       */
} eBspAdcChannelDef;

void BspAdcInit(void);
void BspAdcStartInject(void);
void BspAdcReadRaw(void);
uint16_t BspAdcGetRaw(eBspAdcChannelDef channel);
#endif 