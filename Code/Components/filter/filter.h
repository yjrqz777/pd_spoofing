/**
 * @file    filter.h
 * @brief   与设备无关的常用嵌入式滤波算法接口。
 *
 * @note    本模块属于 Components：不依赖板级外设、操作系统或动态内存。
 *          所有滤波器状态均由调用方持有，可同时创建多个实例。
 */

#ifndef __FILTER_H__
#define __FILTER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/** @brief 中值滤波支持的最大窗口长度，必须使用奇数窗口。 */
#ifndef FILTER_MEDIAN_WINDOW_MAX
#define FILTER_MEDIAN_WINDOW_MAX   (9u)
#endif

/** @brief 滑动平均滤波器。缓冲区由调用方提供。 */
typedef struct
{
    float    *pfBuffer;       /**< 样本环形缓冲区 */
    float     fSum;           /**< 当前窗口内样本之和 */
    uint16_t  u16Window;      /**< 窗口长度 */
    uint16_t  u16Count;       /**< 已写入的有效样本数 */
    uint16_t  u16Index;       /**< 下一次写入位置 */
} tFilterMovingAverage;

/** @brief 中值滤波器。 */
typedef struct
{
    float    afBuffer[FILTER_MEDIAN_WINDOW_MAX]; /**< 样本环形缓冲区 */
    uint16_t u16Window;                          /**< 窗口长度 */
    uint16_t u16Count;                           /**< 已写入的有效样本数 */
    uint16_t u16Index;                           /**< 下一次写入位置 */
} tFilterMedian;

/** @brief 指数移动平均滤波器，也可作为一阶低通滤波器使用。 */
typedef struct
{
    float   fAlpha;         /**< 新样本权重，范围 0 到 1 */
    float   fOutput;        /**< 上一次输出 */
    uint8_t u8Initialized;  /**< 首个样本初始化标志 */
} tFilterEma;

/** @brief 限幅滤波器，用于限制相邻输出的最大变化量。 */
typedef struct
{
    float   fMaxRise;       /**< 单次最大上升量 */
    float   fMaxFall;       /**< 单次最大下降量，保存为正数 */
    float   fOutput;        /**< 上一次输出 */
    uint8_t u8Initialized;  /**< 首个样本初始化标志 */
} tFilterSlewRate;

/** @brief 一维卡尔曼滤波器。 */
typedef struct
{
    float   fProcessNoise;      /**< 过程噪声协方差 Q */
    float   fMeasureNoise;      /**< 测量噪声协方差 R */
    float   fEstimate;          /**< 当前估计值 */
    float   fErrorCovariance;   /**< 当前估计误差协方差 P */
    uint8_t u8Initialized;      /**< 首个样本初始化标志 */
} tFilterKalman1D;

/**
 * @brief  初始化滑动平均滤波器。
 * @param[out] ptFilter  滤波器实例。
 * @param[in]  pfBuffer  调用方提供的样本缓冲区。
 * @param[in]  u16Window 窗口长度，同时也是缓冲区元素数量。
 * @return 1 表示成功，0 表示参数无效。
 */
uint8_t FilterMovingAverageInit(tFilterMovingAverage *ptFilter, float *pfBuffer,
                                uint16_t u16Window);

/** @brief 清空滑动平均滤波器状态。 */
void FilterMovingAverageReset(tFilterMovingAverage *ptFilter);

/** @brief 输入一个样本并返回滑动平均值。 */
float FilterMovingAverageUpdate(tFilterMovingAverage *ptFilter, float fInput);

/**
 * @brief  初始化中值滤波器。
 * @param[out] ptFilter  滤波器实例。
 * @param[in]  u16Window 窗口长度，必须为奇数且不超过 FILTER_MEDIAN_WINDOW_MAX。
 * @return 1 表示成功，0 表示参数无效。
 */
uint8_t FilterMedianInit(tFilterMedian *ptFilter, uint16_t u16Window);

/** @brief 清空中值滤波器状态。 */
void FilterMedianReset(tFilterMedian *ptFilter);

/** @brief 输入一个样本并返回当前有效窗口的中值。 */
float FilterMedianUpdate(tFilterMedian *ptFilter, float fInput);

/**
 * @brief  初始化指数移动平均滤波器。
 * @param[out] ptFilter 滤波器实例。
 * @param[in]  fAlpha   新样本权重，范围 0 到 1；越小越平滑。
 * @return 1 表示成功，0 表示参数无效。
 */
uint8_t FilterEmaInit(tFilterEma *ptFilter, float fAlpha);

/** @brief 清空指数移动平均滤波器状态。 */
void FilterEmaReset(tFilterEma *ptFilter);

/** @brief 输入一个样本并返回指数移动平均值。 */
float FilterEmaUpdate(tFilterEma *ptFilter, float fInput);

/**
 * @brief  初始化限幅滤波器。
 * @param[out] ptFilter 滤波器实例。
 * @param[in]  fMaxRise 单次允许的最大上升量，必须大于等于 0。
 * @param[in]  fMaxFall 单次允许的最大下降量，必须大于等于 0。
 * @return 1 表示成功，0 表示参数无效。
 */
uint8_t FilterSlewRateInit(tFilterSlewRate *ptFilter, float fMaxRise, float fMaxFall);

/** @brief 清空限幅滤波器状态。 */
void FilterSlewRateReset(tFilterSlewRate *ptFilter);

/** @brief 输入一个样本并返回限幅后的值。 */
float FilterSlewRateUpdate(tFilterSlewRate *ptFilter, float fInput);

/**
 * @brief  初始化一维卡尔曼滤波器。
 * @param[out] ptFilter        滤波器实例。
 * @param[in]  fProcessNoise   过程噪声协方差 Q，必须大于等于 0。
 * @param[in]  fMeasureNoise   测量噪声协方差 R，必须大于 0。
 * @param[in]  fInitialError   初始估计误差协方差，必须大于等于 0。
 * @return 1 表示成功，0 表示参数无效。
 */
uint8_t FilterKalman1DInit(tFilterKalman1D *ptFilter, float fProcessNoise,
                           float fMeasureNoise, float fInitialError);

/** @brief 清空一维卡尔曼滤波器状态，保留噪声参数。 */
void FilterKalman1DReset(tFilterKalman1D *ptFilter, float fInitialError);

/** @brief 输入一个测量值并返回一维卡尔曼估计值。 */
float FilterKalman1DUpdate(tFilterKalman1D *ptFilter, float fMeasurement);

#ifdef __cplusplus
}
#endif

#endif /* __FILTER_H__ */
