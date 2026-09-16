/**
 * @file    bsp_lcd.c
 * @brief   LCD 显示底层驱动实现（字段缓冲 + DMA 分段异步输出）
 *******************************************************************************
 * @note   字段缓冲式刷新的实现思路：
 *          1) 应用层调用 BspLcdBeginRefresh() 开启一帧，随后用 BspLcdAddXxx()
 *             追加待显示内容，追加只写内存，不碰 SPI；
 *          2) 每个时间片调用 BspLcdService()，仅在当前字段完成后推进；
 *          3) 所有字段（数字、字符串、区域填充）都走 SPI1 TX DMA 异步输出，
 *             单次调用最多占用 CPU 约 1ms。
 *
 * @warning 这里是本项目"LCD 抢时序"问题的修复点。整屏填充过去是逐像素轮询
 *          输出（240x135 = 32400 次写，独占 CPU 约 80ms），会把主循环里的
 *          周期任务一起拖慢，USB-PD 的 500ms 应答窗口因此被错过。现在填充按
 *          "每行一次 DMA" 推进，字符串按"每块若干字符一次 DMA"推进。
 *******************************************************************************
 */

#include "bsp_lcd.h"
#include "bsp_spi.h"
#include "bsp_time.h"
#include "st7789v/st7789v.h"

/**
 * @brief 一帧内最多允许的待显示操作数。
 * @note  字符串改为异步分块后，每个字符串都会占用一个槽位（长字符串按
 *        LCD_DMA_TEXT_CHUNK_CHARS 还会占用多个），因此容量必须按最坏情况留：
 *          1（整屏填充）+ 12（行标签/开机页，含 pd-spoofing 的两块）
 *          + 4（浮点）+ 3（顶栏 PD/EN 字符串）= 20
 *        之前是 12，改异步后不够用，超出的操作会被静默丢弃。
 */
#define BSP_LCD_FIELD_MAX        (20u)
#define BSP_LCD_FIELD_TIMEOUT_MS (500u)

/** @brief 字段操作类型 */
typedef enum
{
    E_BSP_LCD_OP_NONE = 0,   /**< 空操作（占位） */
    E_BSP_LCD_OP_FILL,       /**< 区域填充 */
    E_BSP_LCD_OP_STRING,     /**< 字符串 */
    E_BSP_LCD_OP_UINT,       /**< 无符号整数 */
    E_BSP_LCD_OP_FLOAT       /**< 浮点数 */
} eBspLcdOpTypeDef;

/** @brief 待显示操作描述 */
typedef struct tBspLcdOpDef
{
    uint16_t        u16X;        /**< 横坐标 */
    uint16_t        u16Y;        /**< 纵坐标 */
    uint16_t        u16Fc;       /**< 前景色 */
    uint16_t        u16Bc;       /**< 背景色 */
    uint8_t         u8Type;      /**< 操作类型，见 eBspLcdOpTypeDef */
    uint8_t         u8Length;    /**< 数字位宽 */
    uint8_t         u8Decimals;  /**< 小数位数 */
    uint8_t         u8SizeY;     /**< 字号 */
    uint8_t         u8TextLen;   /**< 字符串有效长度（不含结尾 0） */
    uint8_t         u8Cursor;    /**< 字符串已送出的字符数 */
    uint8_t         u8CursorRow; /**< 填充已送出的行数 */
    uint32_t        u32Value;    /**< 无符号整数值 */
    float           f32Value;    /**< 浮点数值 */
    char            acText[18];  /**< 字符串缓冲 */
} tBspLcdOpDef;

/** @brief 一份操作副本的字节数（供 memcpy 使用） */
#define BSP_LCD_OP_BYTES  (sizeof(tBspLcdOpDef))

/* ---- 请求帧与正在输出的活动帧 ---- */
static tBspLcdOpDef s_atRequestedOp[BSP_LCD_FIELD_MAX];
static tBspLcdOpDef s_atActiveOp[BSP_LCD_FIELD_MAX];

static uint8_t  s_u8RequestedCount  = 0u;      /**< 请求帧中的操作数 */
static uint8_t  s_u8ActiveCount     = 0u;      /**< 活动帧中的操作数 */
static uint8_t  s_u8ActiveIndex     = 0u;      /**< 活动帧当前操作下标 */
static uint8_t  s_u8RefreshRequested = 0u;     /**< 有新的请求帧待接管 */
static uint8_t  s_u8RefreshInFlight  = 0u;     /**< 正在输出活动帧 */
static uint8_t  s_u8RequestedStateId = 0xFFu;  /**< 请求帧对应的系统状态 */
static uint8_t  s_u8ActiveStateId    = 0xFFu;  /**< 活动帧对应的系统状态 */
static uint32_t s_u32OpStartMs       = 0u;     /**< 当前操作的启动时刻 */

/** @brief 因请求帧槽位不足而被丢弃的操作数（>0 说明 BSP_LCD_FIELD_MAX 偏小） */
static uint16_t s_u16DroppedOps      = 0u;

/**
 * @brief  查询因队列满而丢弃的操作数
 * @return 累计丢弃数量；不为 0 就说明有内容没显示出来
 */
uint16_t BspLcdGetDroppedOps(void)
{
    return s_u16DroppedOps;
}

/* ========================================================================== *
 *  内部函数
 * ========================================================================== */

/**
 * @brief Waits until the LCD DMA buffer is available.
 * @param[in] u32TimeoutMs Maximum wait time in milliseconds.
 * @retval 0 The DMA channel is idle.
 * @retval 1 The wait timed out.
 */
static uint8_t BspLcdWaitDmaIdle(uint32_t u32TimeoutMs)
{
    uint32_t StartMs;

    StartMs = BspTickGetMs();
    while (LCD_IsTransferBusy() != 0u)
    {
        if ((BspTickGetMs() - StartMs) > u32TimeoutMs)
        {
            return 1u;
        }
    }

    return 0u;
}

/**
 * @brief 启动一条操作的第一块输出（全部走 DMA，不阻塞）。
 * @param[in,out] ptOp 操作描述，会更新其推进游标。
 * @retval E_OK    已启动。
 * @retval E_BUSY  DMA 仍占用，下次服务再试。
 * @retval E_ERROR 该操作无法启动。
 */
static eStatusDef BspLcdStartOp(tBspLcdOpDef *ptOp)
{
    switch (ptOp->u8Type)
    {
        case E_BSP_LCD_OP_FILL:
            /* 窗口已在 LCD_Fill() 里设置好，这里逐行 DMA。 */
            ptOp->u8CursorRow = 0u;
            return LCD_FillRowDma(0u, ptOp->u16Fc);

        case E_BSP_LCD_OP_STRING:
            ptOp->u8Cursor = 0u;
            return LCD_ShowStringChunkDma(ptOp->u16X, ptOp->u16Y,
                                          &ptOp->acText[ptOp->u8Cursor],
                                          ptOp->u16Fc, ptOp->u16Bc, ptOp->u8SizeY);

        case E_BSP_LCD_OP_UINT:
            return LCD_ShowIntNumAsync(ptOp->u16X, ptOp->u16Y, ptOp->u32Value,
                                       ptOp->u8Length, ptOp->u16Fc, ptOp->u16Bc,
                                       ptOp->u8SizeY);

        case E_BSP_LCD_OP_FLOAT:
            return LCD_ShowFloatNumAsync(ptOp->u16X, ptOp->u16Y, ptOp->f32Value,
                                         ptOp->u8Length, ptOp->u8Decimals,
                                         ptOp->u16Fc, ptOp->u16Bc,
                                         ptOp->u8SizeY);

        default:
            return E_ERROR;
    }
}

/**
 * @brief 推进一条正在进行中的操作。
 * @param[in,out] ptOp 操作描述。
 * @retval E_OK   已送出下一块。
 * @retval E_BUSY DMA 仍占用，本次不再推进。
 * @retval E_ERROR 本操作已无可推进内容（调用方应先查 BspLcdOpComplete()）。
 */
static eStatusDef BspLcdAdvanceOp(tBspLcdOpDef *ptOp)
{
    eStatusDef eStatus;

    switch (ptOp->u8Type)
    {
        case E_BSP_LCD_OP_FILL:
            /* LCD_FillRowDma() 内部推进行号并维护活动标志。 */
            return LCD_FillRowDma(0u, ptOp->u16Fc);

        case E_BSP_LCD_OP_STRING:
        {
            uint8_t u8Chunk;

            if (ptOp->u8Cursor >= ptOp->u8TextLen)
            {
                return E_ERROR;   /* 已画完 */
            }

            u8Chunk = (uint8_t)(ptOp->u8TextLen - ptOp->u8Cursor);
            if (u8Chunk > LCD_DMA_TEXT_CHUNK_CHARS)
            {
                u8Chunk = LCD_DMA_TEXT_CHUNK_CHARS;
            }

            eStatus = LCD_ShowStringChunkDma(
                (uint16_t)(ptOp->u16X + (uint16_t)ptOp->u8Cursor * (ptOp->u8SizeY / 2u)),
                ptOp->u16Y,
                &ptOp->acText[ptOp->u8Cursor],
                ptOp->u16Fc, ptOp->u16Bc, ptOp->u8SizeY);
            if (eStatus == E_OK)
            {
                ptOp->u8Cursor = (uint8_t)(ptOp->u8Cursor + u8Chunk);
            }
            return eStatus;
        }

        default:
            return E_ERROR;
    }
}

/**
 * @brief 查询一条操作本身是否还有未送出的内容（与 DMA 忙闲无关）。
 * @param[in] ptOp 操作描述。
 * @retval 1 已经全部送出。
 * @retval 0 还有内容待送。
 * @note  帧推进必须用这个判断，不能拿 BspLcdAdvanceOp() 的返回值当"完成"，
 *        因为"已无内容"和"DMA 忙"是两种不同的 E_ERROR/E_BUSY。
 */
static uint8_t BspLcdOpComplete(const tBspLcdOpDef *ptOp)
{
    switch (ptOp->u8Type)
    {
        case E_BSP_LCD_OP_FILL:
            return (LCD_FillActive() == 0u) ? 1u : 0u;

        case E_BSP_LCD_OP_STRING:
            return (ptOp->u8Cursor >= ptOp->u8TextLen) ? 1u : 0u;

        case E_BSP_LCD_OP_UINT:
        case E_BSP_LCD_OP_FLOAT:
            /* 数字块是一次性 DMA，传输结束即完成 */
            return (LCD_IsTransferBusy() == 0u) ? 1u : 0u;

        default:
            return 1u;
    }
}

/**
 * @brief  追加一条操作到请求帧
 * @return 指向新操作的空槽；队列满时返回 0
 */
static tBspLcdOpDef *BspLcdAllocOp(void)
{
    tBspLcdOpDef *ptOp;

    if (s_u8RequestedCount >= BSP_LCD_FIELD_MAX)
    {
        s_u16DroppedOps++;
        return 0;
    }

    ptOp = &s_atRequestedOp[s_u8RequestedCount];
    s_u8RequestedCount++;

    memset(ptOp, 0, sizeof(tBspLcdOpDef));

    return ptOp;
}

/* ========================================================================== *
 *  对外接口
 * ========================================================================== */

void BspLcdInit(void)
{
    uint8_t u8Index;

    for (u8Index = 0u; u8Index < BSP_LCD_FIELD_MAX; u8Index++)
    {
        memset(&s_atRequestedOp[u8Index], 0, sizeof(tBspLcdOpDef));
        memset(&s_atActiveOp[u8Index],    0, sizeof(tBspLcdOpDef));
    }

    s_u8RequestedCount   = 0u;
    s_u8ActiveCount      = 0u;
    s_u8ActiveIndex      = 0u;
    s_u8RefreshRequested = 0u;
    s_u8RefreshInFlight  = 0u;
    s_u8RequestedStateId = 0xFFu;
    s_u8ActiveStateId    = 0xFFu;

    /* ST7789V 初始化（内部含复位时序与整屏填充） */
    st7789v_init();
}

void BspLcdClearScreen(uint16_t u16Color)
{
    /* 不做同步整屏填充：先丢弃尚未接管的请求帧，再登记一条填充操作，
     * 由 BspLcdService() 分批 DMA 输出（整屏约 15 次服务，每次 ~1ms）。 */
    BspLcdCancelRefresh();
    BspLcdAddFill(u16Color);
}

void BspLcdCancelRefresh(void)
{
    s_u8RefreshRequested = 0u;
    s_u8RequestedCount   = 0u;
}

void BspLcdShowString(uint16_t u16X, uint16_t u16Y, const char *pcText,
                      uint16_t u16Fc, uint16_t u16Bc, uint8_t u8SizeY, uint8_t u8Mode)
{
    if (pcText == 0)
    {
        return;
    }

    if (u8Mode == 0u)
    {
        /* 非叠加模式可以整块搬进 DMA 缓冲，登记成异步字段 */
        BspLcdAddString(u16X, u16Y, pcText, u16Fc, u16Bc, u8SizeY);
        return;
    }

    /* 叠加模式需要逐像素判断背景，保留阻塞路径（当前界面未使用）。 */
    BspLcdWaitDmaIdle(BSP_LCD_FIELD_TIMEOUT_MS);
    LCD_ShowString(u16X, u16Y, (const uint8_t *)pcText, u16Fc, u16Bc, u8SizeY, u8Mode);
}

void BspLcdShowUInt(uint16_t u16X, uint16_t u16Y, uint32_t u32Value,
                    uint8_t u8Length, uint16_t u16Fc, uint16_t u16Bc)
{
    BspLcdAddUInt(u16X, u16Y, u32Value, u8Length, u16Fc);
    (void)u16Bc;
}

void BspLcdShowFloat(uint16_t u16X, uint16_t u16Y, float f32Value, uint8_t u8Length,
                     uint8_t u8Decimals, uint16_t u16Fc, uint16_t u16Bc)
{
    BspLcdAddFloat(u16X, u16Y, f32Value, u8Length, u8Decimals, u16Fc);
    (void)u16Bc;
}

void BspLcdBeginRefresh(uint8_t u8StateId)
{
    s_u8RequestedCount   = 0u;
    s_u8RequestedStateId = u8StateId;
    s_u8RefreshRequested = 1u;
}

void BspLcdAddFill(uint16_t u16Color)
{
    tBspLcdOpDef *ptOp = BspLcdAllocOp();

    if (ptOp == 0)
    {
        return;
    }

    ptOp->u8Type = (uint8_t)E_BSP_LCD_OP_FILL;
    ptOp->u16Fc  = u16Color;

    /* 用 1 字节的状态标记本操作是否已经设置过窗口：这里直接借用 u8Length。 */
    ptOp->u8Length = 1u;
}

void BspLcdAddString(uint16_t u16X, uint16_t u16Y, const char *pcText,
                     uint16_t u16Fc, uint16_t u16Bc, uint8_t u8SizeY)
{
    tBspLcdOpDef *ptOp;
    uint8_t       u8Index = 0u;

    if (pcText == 0)
    {
        return;
    }

    ptOp = BspLcdAllocOp();
    if (ptOp == 0)
    {
        return;
    }

    ptOp->u8Type  = (uint8_t)E_BSP_LCD_OP_STRING;
    ptOp->u16X    = u16X;
    ptOp->u16Y    = u16Y;
    ptOp->u16Fc   = u16Fc;
    ptOp->u16Bc   = u16Bc;
    ptOp->u8SizeY = u8SizeY;

    /* 拷贝字符串（含长度截断保护），不保存调用者的指针，避免悬空 */
    while ((pcText[u8Index] != '\0') && (u8Index < (uint8_t)(sizeof(ptOp->acText) - 1u)))
    {
        ptOp->acText[u8Index] = pcText[u8Index];
        u8Index++;
    }
    ptOp->acText[u8Index] = '\0';
    ptOp->u8TextLen = u8Index;
}

void BspLcdAddUInt(uint16_t u16X, uint16_t u16Y, uint32_t u32Value,
                   uint8_t u8Length, uint16_t u16Color)
{
    tBspLcdOpDef *ptOp = BspLcdAllocOp();

    if (ptOp == 0)
    {
        return;
    }

    ptOp->u8Type    = (uint8_t)E_BSP_LCD_OP_UINT;
    ptOp->u16X      = u16X;
    ptOp->u16Y      = u16Y;
    ptOp->u32Value  = u32Value;
    ptOp->u16Fc     = u16Color;
    ptOp->u16Bc     = WHITE;
    ptOp->u8Length  = u8Length;
    ptOp->u8SizeY   = 24u;
}

void BspLcdAddFloat(uint16_t u16X, uint16_t u16Y, float f32Value,
                    uint8_t u8Length, uint8_t u8Decimals, uint16_t u16Color)
{
    tBspLcdOpDef *ptOp = BspLcdAllocOp();

    if (ptOp == 0)
    {
        return;
    }

    ptOp->u8Type     = (uint8_t)E_BSP_LCD_OP_FLOAT;
    ptOp->u16X       = u16X;
    ptOp->u16Y       = u16Y;
    ptOp->f32Value   = f32Value;
    ptOp->u16Fc      = u16Color;
    ptOp->u16Bc      = WHITE;
    ptOp->u8Length   = u8Length;
    ptOp->u8Decimals = u8Decimals;
    ptOp->u8SizeY    = 24u;
}

void BspLcdService(uint8_t u8StateId)
{
    /* 1) 状态已切换：丢弃过期的请求帧与进行中的活动帧 */
    if ((s_u8RefreshRequested != 0u) && (s_u8RequestedStateId != u8StateId))
    {
        s_u8RefreshRequested = 0u;
        s_u8RequestedCount   = 0u;
    }

    if ((s_u8RefreshInFlight != 0u) && (s_u8ActiveStateId != u8StateId))
    {
        s_u8RefreshInFlight = 0u;
        s_u8ActiveIndex     = 0u;
        s_u8ActiveCount     = 0u;
    }

    /* 2) 空闲且有新请求：接管请求帧 */
    if ((s_u8RefreshInFlight == 0u) && (s_u8RefreshRequested != 0u))
    {
        uint8_t u8Index;

        for (u8Index = 0u; u8Index < s_u8RequestedCount; u8Index++)
        {
            s_atActiveOp[u8Index] = s_atRequestedOp[u8Index];
        }

        s_u8ActiveCount      = s_u8RequestedCount;
        s_u8ActiveIndex      = 0u;
        s_u8ActiveStateId    = s_u8RequestedStateId;
        s_u8RefreshInFlight  = (s_u8ActiveCount != 0u) ? 1u : 0u;
        s_u8RefreshRequested = 0u;
        s_u32OpStartMs       = BspTickGetMs();

        /* 接管新帧时立即启动第 0 条操作。 */
        if (s_u8RefreshInFlight != 0u)
        {
            if (BspLcdStartOp(&s_atActiveOp[0]) != E_OK)
            {
                /* E_BUSY 时留着下一次重试；E_ERROR 说明该操作无法启动 */
                if (LCD_IsTransferBusy() == 0u)
                {
                    s_u8RefreshInFlight = 0u;
                    s_u8ActiveCount = 0u;
                }
            }
            return;
        }
    }

    /* 3) 推进当前活动帧 */
    if (s_u8RefreshInFlight != 0u)
    {
        tBspLcdOpDef *ptOp = &s_atActiveOp[s_u8ActiveIndex];

        if (BspLcdOpComplete(ptOp) != 0u)
        {
            /* 本条已全部送出（DMA 可能还在跑），切到下一条。
             * 下一条的启动会自己检查 DMA 忙闲，不忙就直接开，忙则下次再试。 */
            s_u8ActiveIndex++;
            s_u32OpStartMs = BspTickGetMs();

            if (s_u8ActiveIndex >= s_u8ActiveCount)
            {
                s_u8RefreshInFlight = 0u;
                s_u8ActiveIndex = 0u;
                s_u8ActiveCount = 0u;
                return;
            }

            (void)BspLcdStartOp(&s_atActiveOp[s_u8ActiveIndex]);
            return;
        }

        /* 本条未送完：只在 DMA 空闲时补送下一块 */
        if (LCD_IsTransferBusy() == 0u)
        {
            (void)BspLcdAdvanceOp(ptOp);
        }

        if ((BspTickGetMs() - s_u32OpStartMs) > BSP_LCD_FIELD_TIMEOUT_MS)
        {
            s_u8RefreshInFlight = 0u;
            s_u8ActiveIndex     = 0u;
            s_u8ActiveCount     = 0u;
        }
    }
}
