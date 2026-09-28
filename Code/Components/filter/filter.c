/**
 * @file    filter.c
 * @brief   常用嵌入式滤波算法的无动态内存实现。
 */

#include "filter.h"
#include <stddef.h>

uint8_t FilterMovingAverageInit(tFilterMovingAverage *ptFilter, float *pfBuffer,
                                uint16_t u16Window)
{
    if ((ptFilter == NULL) || (pfBuffer == NULL) || (u16Window == 0u))
    {
        return 0u;
    }

    ptFilter->pfBuffer = pfBuffer;
    ptFilter->u16Window = u16Window;
    FilterMovingAverageReset(ptFilter);
    return 1u;
}

void FilterMovingAverageReset(tFilterMovingAverage *ptFilter)
{
    uint16_t u16Index;

    if ((ptFilter == NULL) || (ptFilter->pfBuffer == NULL) || (ptFilter->u16Window == 0u))
    {
        return;
    }

    for (u16Index = 0u; u16Index < ptFilter->u16Window; u16Index++)
    {
        ptFilter->pfBuffer[u16Index] = 0.0f;
    }

    ptFilter->fSum = 0.0f;
    ptFilter->u16Count = 0u;
    ptFilter->u16Index = 0u;
}

float FilterMovingAverageUpdate(tFilterMovingAverage *ptFilter, float fInput)
{
    if ((ptFilter == NULL) || (ptFilter->pfBuffer == NULL) || (ptFilter->u16Window == 0u))
    {
        return fInput;
    }

    if (ptFilter->u16Count < ptFilter->u16Window)
    {
        ptFilter->pfBuffer[ptFilter->u16Index] = fInput;
        ptFilter->fSum += fInput;
        ptFilter->u16Count++;
    }
    else
    {
        ptFilter->fSum -= ptFilter->pfBuffer[ptFilter->u16Index];
        ptFilter->pfBuffer[ptFilter->u16Index] = fInput;
        ptFilter->fSum += fInput;
    }

    ptFilter->u16Index++;
    if (ptFilter->u16Index >= ptFilter->u16Window)
    {
        ptFilter->u16Index = 0u;
    }

    return ptFilter->fSum / (float)ptFilter->u16Count;
}

uint8_t FilterMedianInit(tFilterMedian *ptFilter, uint16_t u16Window)
{
    if ((ptFilter == NULL) || (u16Window == 0u) ||
        (u16Window > FILTER_MEDIAN_WINDOW_MAX) || ((u16Window & 1u) == 0u))
    {
        return 0u;
    }

    ptFilter->u16Window = u16Window;
    FilterMedianReset(ptFilter);
    return 1u;
}

void FilterMedianReset(tFilterMedian *ptFilter)
{
    uint16_t u16Index;

    if ((ptFilter == NULL) || (ptFilter->u16Window == 0u) ||
        (ptFilter->u16Window > FILTER_MEDIAN_WINDOW_MAX))
    {
        return;
    }

    for (u16Index = 0u; u16Index < FILTER_MEDIAN_WINDOW_MAX; u16Index++)
    {
        ptFilter->afBuffer[u16Index] = 0.0f;
    }

    ptFilter->u16Count = 0u;
    ptFilter->u16Index = 0u;
}

float FilterMedianUpdate(tFilterMedian *ptFilter, float fInput)
{
    float afSorted[FILTER_MEDIAN_WINDOW_MAX];
    float fSwap;
    uint16_t u16Index;
    uint16_t u16Scan;

    if ((ptFilter == NULL) || (ptFilter->u16Window == 0u) ||
        (ptFilter->u16Window > FILTER_MEDIAN_WINDOW_MAX) ||
        ((ptFilter->u16Window & 1u) == 0u))
    {
        return fInput;
    }

    ptFilter->afBuffer[ptFilter->u16Index] = fInput;
    ptFilter->u16Index++;
    if (ptFilter->u16Index >= ptFilter->u16Window)
    {
        ptFilter->u16Index = 0u;
    }
    if (ptFilter->u16Count < ptFilter->u16Window)
    {
        ptFilter->u16Count++;
    }

    for (u16Index = 0u; u16Index < ptFilter->u16Count; u16Index++)
    {
        afSorted[u16Index] = ptFilter->afBuffer[u16Index];
    }

    for (u16Index = 1u; u16Index < ptFilter->u16Count; u16Index++)
    {
        fSwap = afSorted[u16Index];
        u16Scan = u16Index;
        while ((u16Scan > 0u) && (afSorted[u16Scan - 1u] > fSwap))
        {
            afSorted[u16Scan] = afSorted[u16Scan - 1u];
            u16Scan--;
        }
        afSorted[u16Scan] = fSwap;
    }

    if ((ptFilter->u16Count & 1u) == 0u)
    {
        return (afSorted[ptFilter->u16Count / 2u - 1u] +
                afSorted[ptFilter->u16Count / 2u]) * 0.5f;
    }

    return afSorted[ptFilter->u16Count / 2u];
}

uint8_t FilterEmaInit(tFilterEma *ptFilter, float fAlpha)
{
    if ((ptFilter == NULL) || (fAlpha < 0.0f) || (fAlpha > 1.0f))
    {
        return 0u;
    }

    ptFilter->fAlpha = fAlpha;
    FilterEmaReset(ptFilter);
    return 1u;
}

void FilterEmaReset(tFilterEma *ptFilter)
{
    if (ptFilter == NULL)
    {
        return;
    }

    ptFilter->fOutput = 0.0f;
    ptFilter->u8Initialized = 0u;
}

float FilterEmaUpdate(tFilterEma *ptFilter, float fInput)
{
    if (ptFilter == NULL)
    {
        return fInput;
    }

    if (ptFilter->u8Initialized == 0u)
    {
        ptFilter->fOutput = fInput;
        ptFilter->u8Initialized = 1u;
    }
    else
    {
        ptFilter->fOutput += ptFilter->fAlpha * (fInput - ptFilter->fOutput);
    }

    return ptFilter->fOutput;
}

uint8_t FilterSlewRateInit(tFilterSlewRate *ptFilter, float fMaxRise, float fMaxFall)
{
    if ((ptFilter == NULL) || (fMaxRise < 0.0f) || (fMaxFall < 0.0f))
    {
        return 0u;
    }

    ptFilter->fMaxRise = fMaxRise;
    ptFilter->fMaxFall = fMaxFall;
    FilterSlewRateReset(ptFilter);
    return 1u;
}

void FilterSlewRateReset(tFilterSlewRate *ptFilter)
{
    if (ptFilter == NULL)
    {
        return;
    }

    ptFilter->fOutput = 0.0f;
    ptFilter->u8Initialized = 0u;
}

float FilterSlewRateUpdate(tFilterSlewRate *ptFilter, float fInput)
{
    float fDelta;

    if (ptFilter == NULL)
    {
        return fInput;
    }

    if (ptFilter->u8Initialized == 0u)
    {
        ptFilter->fOutput = fInput;
        ptFilter->u8Initialized = 1u;
        return ptFilter->fOutput;
    }

    fDelta = fInput - ptFilter->fOutput;
    if (fDelta > ptFilter->fMaxRise)
    {
        ptFilter->fOutput += ptFilter->fMaxRise;
    }
    else if (fDelta < -ptFilter->fMaxFall)
    {
        ptFilter->fOutput -= ptFilter->fMaxFall;
    }
    else
    {
        ptFilter->fOutput = fInput;
    }

    return ptFilter->fOutput;
}

uint8_t FilterKalman1DInit(tFilterKalman1D *ptFilter, float fProcessNoise,
                           float fMeasureNoise, float fInitialError)
{
    if ((ptFilter == NULL) || (fProcessNoise < 0.0f) ||
        (fMeasureNoise <= 0.0f) || (fInitialError < 0.0f))
    {
        return 0u;
    }

    ptFilter->fProcessNoise = fProcessNoise;
    ptFilter->fMeasureNoise = fMeasureNoise;
    FilterKalman1DReset(ptFilter, fInitialError);
    return 1u;
}

void FilterKalman1DReset(tFilterKalman1D *ptFilter, float fInitialError)
{
    if ((ptFilter == NULL) || (fInitialError < 0.0f))
    {
        return;
    }

    ptFilter->fEstimate = 0.0f;
    ptFilter->fErrorCovariance = fInitialError;
    ptFilter->u8Initialized = 0u;
}

float FilterKalman1DUpdate(tFilterKalman1D *ptFilter, float fMeasurement)
{
    float fGain;
    float fDenominator;

    if (ptFilter == NULL)
    {
        return fMeasurement;
    }

    if (ptFilter->u8Initialized == 0u)
    {
        ptFilter->fEstimate = fMeasurement;
        ptFilter->u8Initialized = 1u;
        return ptFilter->fEstimate;
    }

    ptFilter->fErrorCovariance += ptFilter->fProcessNoise;
    fDenominator = ptFilter->fErrorCovariance + ptFilter->fMeasureNoise;
    if (fDenominator <= 0.0f)
    {
        return ptFilter->fEstimate;
    }

    fGain = ptFilter->fErrorCovariance / fDenominator;
    ptFilter->fEstimate += fGain * (fMeasurement - ptFilter->fEstimate);
    ptFilter->fErrorCovariance *= (1.0f - fGain);

    return ptFilter->fEstimate;
}
