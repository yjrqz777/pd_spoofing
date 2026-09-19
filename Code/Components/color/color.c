/**
 * @file    color.c
 * @brief   HSV 转 8bit RGB 的整型实现。
 *
 * @note    全部使用整数运算，不引入浮点，适合在没有 FPU 的 MCU 上周期性调用。
 */

#include "color.h"
#include <stddef.h>

/**
 * @brief  生成 HSV 模型中的一条斜坡。
 * @param[in] u8Value 明度（V）分量。
 * @param[in] u8Sat   饱和度（S）分量。
 * @param[in] u16Part 要扣掉的饱和度比例，单位为 1/60 扇区。
 * @return 斜坡值：u16Part 为 0 时等于 u8Value，为 60 时等于完全去饱和的 P 点。
 * @note   模型里的三个关键点都由本函数产生：P 传入 60，Q 传入扇区内位置 f，
 *         T 传入 60 - f。
 */
static uint8_t ColorRamp(uint8_t u8Value, uint8_t u8Sat, uint16_t u16Part)
{
    uint16_t Drop;

    Drop = (uint16_t)(((uint32_t)u8Sat * (uint32_t)u16Part) / 60u);   /* 0..255 的衰减量 */
    return (uint8_t)(((uint32_t)u8Value *
                      (uint32_t)((uint16_t)COLOR_SV_MAX - Drop)) / (uint32_t)COLOR_SV_MAX);
}

/**
 * @brief  把 HSV 颜色换算成 8bit 的 RGB 分量。
 * @param[in]  u16H 色相，单位为度；超过 COLOR_HUE_MAX 时按圆周回绕。
 * @param[in]  u8S  饱和度，0-COLOR_SV_MAX；为 0 时得到灰阶。
 * @param[in]  u8V  明度，0-COLOR_SV_MAX；为 0 时得到全黑。
 * @param[out] pu8R 输出红色分量
 * @param[out] pu8G 输出绿色分量
 * @param[out] pu8B 输出蓝色分量
 * @note   色相被切成 6 个 60 度扇区：扇区内一个分量保持在 V，另一个沿
 *         ColorRamp() 生成的 Q/T 斜坡升降，第三个分量停在 P 点。
 * @warning 三个输出指针中只要有一个为 NULL 就放弃本次换算，且一个输出都不写；
 *          调用方只有在传入三个有效指针后才应认为输出有效。
 */
void ColorHsvToRgb(uint16_t u16H, uint8_t u8S, uint8_t u8V,
                   uint8_t * pu8R, uint8_t * pu8G, uint8_t * pu8B)
{
    uint16_t Sextant;
    uint16_t Fraction;
    uint8_t  Min;
    uint8_t  Rise;
    uint8_t  Fall;
    uint8_t  Red;
    uint8_t  Green;
    uint8_t  Blue;

    if ((pu8R == NULL) || (pu8G == NULL) || (pu8B == NULL))
    {
        return;
    }

    u16H %= (uint16_t)(COLOR_HUE_MAX + 1u);        /* 0..359：超出范围时按圆周回绕 */
    Sextant = (uint16_t)(u16H / 60u);              /* 0..5：落在哪个 60 度扇区 */
    Fraction = (uint16_t)(u16H % 60u);             /* 0..59：扇区内的位置 f */

    Min = ColorRamp(u8V, u8S, 60u);                /* P：完全去饱和的那一端 */
    Rise = ColorRamp(u8V, u8S, Fraction);          /* Q：向 V 上升的斜坡 */
    Fall = ColorRamp(u8V, u8S, (uint16_t)(60u - Fraction)); /* T：由 V 下降的斜坡 */

    switch (Sextant)
    {
        case 0u:                                   /* 0~59 度：红最大，绿上升 */
            Red = u8V;
            Green = Fall;
            Blue = Min;
            break;

        case 1u:                                   /* 60~119 度：黄到绿，红下降 */
            Red = Rise;
            Green = u8V;
            Blue = Min;
            break;

        case 2u:                                   /* 120~179 度：绿最大，蓝上升 */
            Red = Min;
            Green = u8V;
            Blue = Fall;
            break;

        case 3u:                                   /* 180~239 度：青到蓝，绿下降 */
            Red = Min;
            Green = Rise;
            Blue = u8V;
            break;

        case 4u:                                   /* 240~299 度：蓝最大，红上升 */
            Red = Fall;
            Green = Min;
            Blue = u8V;
            break;

        default:                                   /* 300~359 度：品红回红，蓝下降 */
            Red = u8V;
            Green = Min;
            Blue = Rise;
            break;
    }

    *pu8R = Red;
    *pu8G = Green;
    *pu8B = Blue;
}
