/**
 * @file dev_ws2812.h
 * @brief WS2812 灯珠设备层接口与实现。
 * @author 段志炫
 * @date 2026-10-15
 * @version 0.1.1
 * @par 修改记录
 * 2026-10-15 按项目规范补充中文 Doxygen 注释。
 */
#ifndef __DEV_WS2812_H__
#define __DEV_WS2812_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "user_global.h"

#define DEV_WS2812_FRAME_LEN   ((WS2812_PIXEL_NUM) * 3u)   /* GRB 字节数：4 颗 -> 12 */

void       DevWs2812Init(void);
/**
 * @brief DevWs2812SetPixel 接口处理函数。
 * @param[in] u16Index 参数值，单位和范围按接口定义。
 * @param[in] u8Red 参数值，单位和范围按接口定义。
 * @param[in] u8Green 参数值，单位和范围按接口定义。
 * @param[in] u8Blue 参数值，单位和范围按接口定义。
 * @return 返回状态或计算结果，具体含义见返回值定义。
 * @note 调用时应遵循模块初始化顺序与运行上下文要求。
 */
eStatusDef DevWs2812SetPixel(uint16_t u16Index, uint8_t u8Red, uint8_t u8Green, uint8_t u8Blue);
/**
 * @brief DevWs2812SetMask 接口处理函数。
 * @param[in] u8Mask 参数值，单位和范围按接口定义。
 * @param[in] u8Red 参数值，单位和范围按接口定义。
 * @param[in] u8Green 参数值，单位和范围按接口定义。
 * @param[in] u8Blue 参数值，单位和范围按接口定义。
 * @return 返回状态或计算结果，具体含义见返回值定义。
 * @note 调用时应遵循模块初始化顺序与运行上下文要求。
 */
eStatusDef DevWs2812SetMask(uint8_t u8Mask, uint8_t u8Red, uint8_t u8Green, uint8_t u8Blue);
/**
 * @brief DevWs2812Fill 接口处理函数。
 * @param[in] u8Red 参数值，单位和范围按接口定义。
 * @param[in] u8Green 参数值，单位和范围按接口定义。
 * @param[in] u8Blue 参数值，单位和范围按接口定义。
 * @return 返回状态或计算结果，具体含义见返回值定义。
 * @note 调用时应遵循模块初始化顺序与运行上下文要求。
 */
eStatusDef DevWs2812Fill(uint8_t u8Red, uint8_t u8Green, uint8_t u8Blue);
/**
 * @brief DevWs2812Flush 接口处理函数。
无输入参数。
 * @return 返回状态或计算结果，具体含义见返回值定义。
 * @note 调用时应遵循模块初始化顺序与运行上下文要求。
 */
eStatusDef DevWs2812Flush(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_WS2812_H__ */
