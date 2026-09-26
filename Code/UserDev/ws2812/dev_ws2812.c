/**
 * @file    dev_ws2812.c
 * @brief   WS2812 灯珠阵列设备层：颜色装配成 GRB 帧并通过 BSP 发送。
 */

#include "dev_ws2812.h"
#include "UserBsp/bsp_ws2812.h"      /* BspWs2812LoadBytes/BspWs2812Show */

/** @brief 一帧的 GRB 字节缓冲，顺序即器件要求的发送顺序 */
static uint8_t s_au8Frame[DEV_WS2812_FRAME_LEN] = {0u};

/** @brief 帧缓冲是否有未发送的修改（0=无, 1=有） */
static uint8_t s_u8Dirty = 0u;

/**
 * @brief  Initializes the pixel frame buffer to all-off.
 */
void DevWs2812Init(void)
{
    uint16_t Index;

    for (Index = 0u; Index < DEV_WS2812_FRAME_LEN; Index++)
    {
        s_au8Frame[Index] = 0u;                  /* 上电全灭，避免上电瞬间亮一帧随机色 */
    }

    s_u8Dirty = 0u;
}

/**
 * @brief  Sets one pixel in the pending frame.
 * @param[in] u16Index Pixel index from zero through WS2812_PIXEL_NUM - 1.
 * @param[in] u8Red    Red component.
 * @param[in] u8Green  Green component.
 * @param[in] u8Blue   Blue component.
 * @retval E_OK    The pixel was stored.
 * @retval E_ERROR The index is out of range.
 */
eStatusDef DevWs2812SetPixel(uint16_t u16Index, uint8_t u8Red, uint8_t u8Green, uint8_t u8Blue)
{
    uint8_t *pu8Pixel;

    if (u16Index >= (uint16_t)WS2812_PIXEL_NUM)
    {
        return E_ERROR;
    }

    /* 器件按 GRB 顺序接收：先绿、再红、后蓝 */
    pu8Pixel = &s_au8Frame[(uint16_t)(u16Index * 3u)];
    pu8Pixel[0] = u8Green;
    pu8Pixel[1] = u8Red;
    pu8Pixel[2] = u8Blue;
    s_u8Dirty = 1u;

    return E_OK;
}

/**
 * @brief  Sets every pixel selected by a bit mask to the same color.
 * @param[in] u8Mask  Bit mask: bit 0 selects pixel 0, bit 1 selects pixel 1, and so on.
 * @param[in] u8Red   Red component.
 * @param[in] u8Green Green component.
 * @param[in] u8Blue  Blue component.
 * @retval E_OK Always; bits at or above WS2812_PIXEL_NUM are ignored.
 */
eStatusDef DevWs2812SetMask(uint8_t u8Mask, uint8_t u8Red, uint8_t u8Green, uint8_t u8Blue)
{
    uint16_t Index;

    for (Index = 0u; Index < (uint16_t)WS2812_PIXEL_NUM; Index++)
    {
        if ((u8Mask & (uint8_t)(1u << Index)) != 0u)
        {
            (void)DevWs2812SetPixel(Index, u8Red, u8Green, u8Blue);
        }
    }

    return E_OK;
}

/**
 * @brief  Sets every pixel in the pending frame to the same color.
 */
eStatusDef DevWs2812Fill(uint8_t u8Red, uint8_t u8Green, uint8_t u8Blue)
{
    uint16_t Index;

    for (Index = 0u; Index < (uint16_t)WS2812_PIXEL_NUM; Index++)
    {
        (void)DevWs2812SetPixel(Index, u8Red, u8Green, u8Blue);
    }

    return E_OK;
}

/**
 * @brief  Sends the pending frame through the BSP layer.
 * @retval E_OK    The frame was handed to the driver.
 * @retval E_BUSY  The previous frame is still in flight; the pending changes are kept,
 *                 so the caller may simply retry on the next tick.
 * @retval E_ERROR The driver rejected the frame.
 * @note   A frame with no changes is not sent at all.
 */
eStatusDef DevWs2812Flush(void)
{
    eStatusDef eStatus;

    if (s_u8Dirty == 0u)
    {
        return E_OK;                             /* 没改动，别占总线 */
    }

    if (BspWs2812IsIdle() == 0u)
    {
        return E_BUSY;                           /* 上一帧还在发：改动保留，下一拍重试 */
    }

    eStatus = BspWs2812LoadBytes(s_au8Frame, DEV_WS2812_FRAME_LEN);
    if (eStatus != E_OK)
    {
        return eStatus;
    }

    eStatus = BspWs2812Show();
    if (eStatus == E_OK)
    {
        s_u8Dirty = 0u;                          /* 交出去了才算发完 */
    }

    return eStatus;
}