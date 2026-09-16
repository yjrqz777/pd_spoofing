/**
 * @file    ui_dashboard.c
 * @brief   240x135 横屏仪表界面实现（静态布局 + 局部刷新）
 *******************************************************************************
 * @note    绘制策略：
 *            1) Init() 阻塞地整屏清成页面背景色，然后画一次卡片底色/边框/标签；
 *            2) SetData() 只拷贝数据，不碰 SPI；
 *            3) Refresh() 把数值格式化成字符串，与上一帧比较，只有变了才
 *               "先用底色清矩形，再重绘数值与单位"。
 *
 *          为什么可以阻塞：每次 Refresh 的重绘量都是局部小矩形（最大
 *          88x24 像素 ≈ 4.2KB SPI 数据 ≈ 3.5ms @12MHz），而且只在数值变化时
 *          发生；整屏填充只在 Init() 里做一次（约 45ms）。
 *          所有数值都走整数格式化，不经过浮点 printf/snprintf。
 *******************************************************************************
 */

#include "ui_dashboard.h"
#include "ui_font.h"
#include "fonts/font_inter_8.h"
#include "fonts/font_inter_16.h"
#include "fonts/font_inter_24.h"
#include "st7789v/st7789v.h"

/* ========================================================================== *
 *  1. RGB565 调色板（对应实施计划 §5；字节序交换只在底层 SPI 发送处处理）
 * ========================================================================== */

#define UI_COLOR_PAGE_BG      ((uint16_t)0x0041u)   /**< 页面背景 #070B0F */
#define UI_COLOR_CARD_BG      ((uint16_t)0x0883u)   /**< 卡片背景 #0C1218 */
#define UI_COLOR_BORDER       ((uint16_t)0x1946u)   /**< 边框/分隔线 #1B2832 */
#define UI_COLOR_TEXT_DIM     ((uint16_t)0x7411u)   /**< 次要文字 #70818D */
#define UI_COLOR_TEXT_MAIN    ((uint16_t)0xEFBFu)   /**< 主要文字 #EEF6F8 */
#define UI_COLOR_VBUS         ((uint16_t)0x46DCu)   /**< VBUS 青色 #45D8E6 */
#define UI_COLOR_VOUT_ON      ((uint16_t)0x6733u)   /**< VOUT/ON 绿色 #62E69A */
#define UI_COLOR_POWER        ((uint16_t)0xFE2Cu)   /**< 功率橙色 #FFC766 */
#define UI_COLOR_SWITCH_BG    ((uint16_t)0x2226u)   /**< 开关底色 #214737 */

/* ========================================================================== *
 *  2. 布局常量（对应实施计划 §4.1，单位：像素）
 *     布局为满屏铺满：内容不预留外围留白，左右两侧直接贴到 x=0/x=239，
 *     底部贴到 y=134（唯一例外是顶部状态栏文字上方的 5~6px 呼吸空间）。
 *     所有矩形都已核算过：内容右边界 < 区域右边界 - 4px。
 * ========================================================================== */

/** @brief 屏幕尺寸（与 ST7789V 驱动保持一致） */
#define UI_LAYOUT_W           (240u)
#define UI_LAYOUT_H           (135u)

/* ---- 顶部状态栏 ---- */
#define UI_BAR_DOT_X          (6u)
#define UI_BAR_DOT_Y          (9u)    /**< 与标题大写字母的视觉中心（y=12）对齐 */
#define UI_BAR_DOT_SIZE       (6u)
#define UI_BAR_TITLE_X        (18u)
#define UI_BAR_BASELINE_Y     (18u)   /**< 16px 文字的基线（墨迹 6..18） */
#define UI_BAR_LINE_X         (0u)
#define UI_BAR_LINE_Y         (23u)
#define UI_BAR_LINE_W         (UI_LAYOUT_W)

/* 顶部状态栏右侧的连接状态（16px，右对齐到屏幕右侧 6px 内边距） */
#define UI_BAR_STATUS_RIGHT   (234u)
#define UI_BAR_STATUS_RECT_X  (158u)
#define UI_BAR_STATUS_RECT_Y  (4u)
#define UI_BAR_STATUS_RECT_W  (78u)
#define UI_BAR_STATUS_RECT_H  (19u)

/* ---- VBUS / VOUT 卡片 ---- */
#define UI_CARD_Y             (28u)
#define UI_CARD_W             (118u)  /**< (240 - 间距 4) / 2 */
#define UI_CARD_H             (54u)
#define UI_CARD_GAP           (4u)
#define UI_CARD_PAD           (10u)
#define UI_VBUS_X             (0u)
#define UI_VOUT_X             ((uint16_t)(UI_VBUS_X + UI_CARD_W + UI_CARD_GAP))
#define UI_CARD_LABEL_BASE_Y  ((uint16_t)(UI_CARD_Y + 15u))
#define UI_CARD_VALUE_BASE_Y  ((uint16_t)(UI_CARD_Y + 41u))
#define UI_CARD_VALUE_RECT_Y  ((uint16_t)(UI_CARD_Y + 23u))
#define UI_CARD_VALUE_RECT_W  (88u)   /**< "12.08" 72px + 间距 4 + "V" 6px = 82px */
#define UI_CARD_VALUE_RECT_H  (24u)

/* ---- POWER / IBUS / OUTPUT 三个平铺区域 ---- */
#define UI_TILE_Y             (87u)
#define UI_TILE_H             (48u)
#define UI_TILE_GAP           (4u)
#define UI_TILE_PAD           (8u)
#define UI_TILE_LABEL_BASE_Y  ((uint16_t)(UI_TILE_Y + 15u))
#define UI_TILE_VALUE_BASE_Y  ((uint16_t)(UI_TILE_Y + 38u))
#define UI_TILE_VALUE_RECT_Y  ((uint16_t)(UI_TILE_Y + 26u))
#define UI_TILE_VALUE_RECT_H  (16u)

#define UI_POWER_X            (0u)
#define UI_POWER_W            (77u)
#define UI_POWER_VALUE_X      ((uint16_t)(UI_POWER_X + UI_TILE_PAD))
#define UI_POWER_VALUE_RECT_W (60u)   /**< "88.8" 38px + 间距 4 + "W" 8px = 50px */

#define UI_IBUS_X             ((uint16_t)(UI_POWER_X + UI_POWER_W + UI_TILE_GAP))
#define UI_IBUS_W             (77u)
#define UI_IBUS_VALUE_X       ((uint16_t)(UI_IBUS_X + UI_TILE_PAD))
#define UI_IBUS_VALUE_RECT_W  (50u)   /**< "9.99" 38px + 间距 4 + "A" 6px = 48px */

#define UI_OUTPUT_X           ((uint16_t)(UI_IBUS_X + UI_IBUS_W + UI_TILE_GAP))
#define UI_OUTPUT_W           ((uint16_t)(UI_LAYOUT_W - UI_OUTPUT_X))
#define UI_OUTPUT_TEXT_X      ((uint16_t)(UI_OUTPUT_X + UI_TILE_PAD))
#define UI_OUTPUT_TEXT_W      (32u)   /**< "OFF" 30px；右侧留给开关槽 */

/* ---- 数值与单位之间的间距 ---- */
#define UI_UNIT_GAP           (4u)

/* ---- 开关槽 ---- */
#define UI_SWITCH_RECT_X      (213u)
#define UI_SWITCH_RECT_Y      (114u)
#define UI_SWITCH_RECT_W      (18u)
#define UI_SWITCH_RECT_H      (10u)
#define UI_SWITCH_RADIUS      (3u)
#define UI_SWITCH_KNOB_SIZE   (6u)
#define UI_SWITCH_KNOB_INSET  (2u)

/** @brief 母线电压低于该值时认为"没有输入"，顶部状态栏留空 */
#define UI_VBUS_ONLINE_MV     (4000u)

/* ========================================================================== *
 *  3. 模块内部状态
 * ========================================================================== */

/** @brief 上一帧已显示的内容（只用于比较，决定要不要重绘） */
typedef struct
{
    char    acVbus[8];         /**< 例如 "12.08" */
    char    acVout[8];
    char    acIbus[8];
    char    acPower[8];
    bool    bOutputEnabled;    /**< 开关状态 */
    bool    bLinkUp;           /**< 顶部状态栏的连接状态（输入电压是否存在） */
} UiDashboardCache;

static UiPowerData      s_tData;        /**< 最近一次提交的数据 */
static UiDashboardCache s_tCache;       /**< 上一帧已经画在屏幕上的内容 */
static bool             s_bReady;       /**< Init() 是否已经完成 */
static bool             s_bOutputDrawn; /**< 开关槽是否已经在屏幕上画过 */

/* ========================================================================== *
 *  4. 整数格式化（不使用浮点 printf）
 * ========================================================================== */

/**
 * @brief  写入固定位数的十进制数字（前导零填充）
 * @param[out] pcText   输出缓冲
 * @param[in]  u32Value 数值
 * @param[in]  u8Digits 位数
 */
static void UiDashboard_PutUInt(char *pcText, uint32_t u32Value, uint8_t u8Digits)
{
    uint8_t u8Index;

    for (u8Index = 0u; u8Index < u8Digits; u8Index++)
    {
        pcText[u8Digits - 1u - u8Index] = (char)('0' + (u32Value % 10u));
        u32Value /= 10u;
    }
}

/**
 * @brief  格式化电压为 "xx.xx"
 * @param[out] pcText       输出缓冲（至少 7 字节）
 * @param[in]  u32MilliVolt 电压（mV）
 * @param[in]  bValid       数据是否有效
 * @note   固定 5 字符宽度，配合统一数字前进宽度，数值变化时不会左右跳动；
 *         超过 99.99V 时饱和，避免溢出到相邻区域。
 */
static void UiDashboard_FormatVoltage(char *pcText, uint32_t u32MilliVolt, bool bValid)
{
    if (bValid == false)
    {
        strcpy(pcText, "--.--");
        return;
    }

    if (u32MilliVolt > 99999u)
    {
        u32MilliVolt = 99999u;
    }

    UiDashboard_PutUInt(&pcText[0], u32MilliVolt / 1000u, 2u);
    pcText[2] = '.';
    UiDashboard_PutUInt(&pcText[3], (u32MilliVolt % 1000u) / 10u, 2u);
    pcText[5] = '\0';
}

/**
 * @brief  格式化电流为 "x.xx"
 * @param[out] pcText      输出缓冲（至少 6 字节）
 * @param[in]  u32MilliAmp 电流（mA）
 * @param[in]  bValid      数据是否有效
 */
static void UiDashboard_FormatCurrent(char *pcText, uint32_t u32MilliAmp, bool bValid)
{
    if (bValid == false)
    {
        strcpy(pcText, "--.--");
        return;
    }

    if (u32MilliAmp > 9999u)
    {
        u32MilliAmp = 9999u;
    }

    UiDashboard_PutUInt(&pcText[0], u32MilliAmp / 1000u, 1u);
    pcText[1] = '.';
    UiDashboard_PutUInt(&pcText[2], (u32MilliAmp % 1000u) / 10u, 2u);
    pcText[4] = '\0';
}

/**
 * @brief  格式化功率为 "xx.x"
 * @param[out] pcText       输出缓冲（至少 6 字节）
 * @param[in]  u32MilliWatt 功率（mW），由 VBUS x IBUS 得到
 * @param[in]  bValid       数据是否有效
 * @note   这里是**输入功率**：当前硬件只有 IBUS 采样，没有 IOUT，
 *         不能把它当成输出功率显示。
 */
static void UiDashboard_FormatPower(char *pcText, uint32_t u32MilliWatt, bool bValid)
{
    if (bValid == false)
    {
        strcpy(pcText, "--.-");
        return;
    }

    /* 显示单位是 0.1W：先换算成"十分之一瓦"，再做四舍五入 */
    u32MilliWatt = (u32MilliWatt + 50u) / 100u;
    if (u32MilliWatt > 999u)
    {
        u32MilliWatt = 999u;
    }

    UiDashboard_PutUInt(&pcText[0], u32MilliWatt / 10u, 2u);
    pcText[2] = '.';
    UiDashboard_PutUInt(&pcText[3], u32MilliWatt % 10u, 1u);
    pcText[4] = '\0';
}

/* ========================================================================== *
 *  5. 绘制辅助
 * ========================================================================== */

/**
 * @brief  清空一个矩形（用底色覆盖，不做差值擦除）
 */
static void UiDashboard_ClearRect(uint16_t u16X, uint16_t u16Y, uint16_t u16W, uint16_t u16H,
                                  uint16_t u16Color)
{
    LCD_FillRect(u16X, u16Y, u16W, u16H, u16Color);
}

/**
 * @brief  填充一个带圆角的矩形
 * @param[in] u16Radius 圆角半径（0 表示直角）
 * @note   半径很小，用"中间主干一次填充 + 上下各 Radius 行倒角"近似，
 *         避免逐像素画圆带来的大量 SPI 窗口设置。
 */
static void UiDashboard_FillRounded(uint16_t u16X, uint16_t u16Y, uint16_t u16W, uint16_t u16H,
                                    uint16_t u16Radius, uint16_t u16Color)
{
    uint16_t u16Row;

    if ((u16W == 0u) || (u16H == 0u))
    {
        return;
    }

    if ((u16Radius == 0u) || (u16H <= (uint16_t)(2u * u16Radius)) ||
        (u16W <= (uint16_t)(2u * u16Radius)))
    {
        LCD_FillRect(u16X, u16Y, u16W, u16H, u16Color);
        return;
    }

    LCD_FillRect(u16X, (uint16_t)(u16Y + u16Radius), u16W,
                 (uint16_t)(u16H - 2u * u16Radius), u16Color);

    for (u16Row = 0u; u16Row < u16Radius; u16Row++)
    {
        uint16_t u16Inset = (uint16_t)(u16Radius - u16Row);
        uint16_t u16RowW  = (uint16_t)(u16W - 2u * u16Inset);

        LCD_FillRect((uint16_t)(u16X + u16Inset), (uint16_t)(u16Y + u16Row),
                     u16RowW, 1u, u16Color);
        LCD_FillRect((uint16_t)(u16X + u16Inset), (uint16_t)(u16Y + u16H - 1u - u16Row),
                     u16RowW, 1u, u16Color);
    }
}

/**
 * @brief  绘制一个带底色与 1px 边框的卡片
 */
static void UiDashboard_DrawCard(uint16_t u16X, uint16_t u16Y, uint16_t u16W, uint16_t u16H)
{
    LCD_FillRect(u16X, u16Y, u16W, u16H, UI_COLOR_CARD_BG);
    LCD_FillRect(u16X, u16Y, u16W, 1u, UI_COLOR_BORDER);
    LCD_FillRect(u16X, (uint16_t)(u16Y + u16H - 1u), u16W, 1u, UI_COLOR_BORDER);
    LCD_FillRect(u16X, u16Y, 1u, u16H, UI_COLOR_BORDER);
    LCD_FillRect((uint16_t)(u16X + u16W - 1u), u16Y, 1u, u16H, UI_COLOR_BORDER);
}

/**
 * @brief  绘制"数值 + 单位"
 * @param[in] u16ValueX      数值左边界
 * @param[in] u16BaselineY   基线
 * @param[in] ptValueFont    数值字库
 * @param[in] pcValue        数值文本
 * @param[in] u16ValueColor  数值颜色
 * @param[in] pcUnit         单位文本（8px 弱化灰色，与数值同基线）
 */
static void UiDashboard_DrawValueWithUnit(uint16_t u16ValueX, uint16_t u16BaselineY,
                                          const UiFont *ptValueFont, const char *pcValue,
                                          uint16_t u16ValueColor, const char *pcUnit)
{
    uint16_t u16UnitX;

    UiFont_DrawText((int16_t)u16ValueX, (int16_t)u16BaselineY, ptValueFont,
                    u16ValueColor, UI_COLOR_CARD_BG, pcValue);

    u16UnitX = (uint16_t)(u16ValueX + UiFont_MeasureText(ptValueFont, pcValue) + UI_UNIT_GAP);
    UiFont_DrawText((int16_t)u16UnitX, (int16_t)u16BaselineY, &g_tUiFontInter8,
                    UI_COLOR_TEXT_DIM, UI_COLOR_CARD_BG, pcUnit);
}

/* ========================================================================== *
 *  6. 动态内容
 * ========================================================================== */

/**
 * @brief  绘制顶部状态栏右侧的连接状态
 * @note   判据是"输入母线电压存在"，不做任何协议就绪的硬编码承诺；
 *         没有输入时整块留空。文字用 16px 字库，与标题同号，保证可读性。
 */
static void UiDashboard_DrawLinkStatus(void)
{
    const char *pcText;
    uint16_t    u16X;

    pcText = (s_tData.measurements_valid != false && s_tData.vbus_mv >= UI_VBUS_ONLINE_MV)
                 ? "ONLINE" : "";

    UiDashboard_ClearRect(UI_BAR_STATUS_RECT_X, UI_BAR_STATUS_RECT_Y,
                          UI_BAR_STATUS_RECT_W, UI_BAR_STATUS_RECT_H, UI_COLOR_PAGE_BG);

    if (pcText[0] == '\0')
    {
        return;
    }

    u16X = (uint16_t)(UI_BAR_STATUS_RIGHT - UiFont_MeasureText(&g_tUiFontInter16, pcText));
    UiFont_DrawText((int16_t)u16X, (int16_t)UI_BAR_BASELINE_Y,
                    &g_tUiFontInter16, UI_COLOR_VOUT_ON, UI_COLOR_PAGE_BG, pcText);
}

/**
 * @brief  绘制 OUTPUT 区域的 ON/OFF 文字与开关槽
 * @note   ON：绿色文字，绿色滑块在槽右侧；OFF：灰色文字，灰色滑块在槽左侧。
 *         文字矩形与开关槽一起重绘，避免 OFF->ON 时残留上一个状态。
 */
static void UiDashboard_DrawOutputState(void)
{
    uint16_t    u16SlotColor;
    uint16_t    u16KnobColor;
    uint16_t    u16TextColor;
    uint16_t    u16KnobX;
    const char *pcText;

    if (s_tData.output_enabled != false)
    {
        u16SlotColor = UI_COLOR_SWITCH_BG;
        u16KnobColor = UI_COLOR_VOUT_ON;
        u16TextColor = UI_COLOR_VOUT_ON;
        u16KnobX     = (uint16_t)(UI_SWITCH_RECT_X + UI_SWITCH_RECT_W -
                                  UI_SWITCH_KNOB_INSET - UI_SWITCH_KNOB_SIZE);
        pcText       = "ON";
    }
    else
    {
        u16SlotColor = UI_COLOR_BORDER;
        u16KnobColor = UI_COLOR_TEXT_DIM;
        u16TextColor = UI_COLOR_TEXT_DIM;
        u16KnobX     = (uint16_t)(UI_SWITCH_RECT_X + UI_SWITCH_KNOB_INSET);
        pcText       = "OFF";
    }

    UiDashboard_ClearRect(UI_OUTPUT_TEXT_X, UI_TILE_VALUE_RECT_Y,
                          UI_OUTPUT_TEXT_W, UI_TILE_VALUE_RECT_H, UI_COLOR_CARD_BG);
    UiFont_DrawText((int16_t)UI_OUTPUT_TEXT_X, (int16_t)UI_TILE_VALUE_BASE_Y,
                    &g_tUiFontInter16, u16TextColor, UI_COLOR_CARD_BG, pcText);

    UiDashboard_FillRounded(UI_SWITCH_RECT_X, UI_SWITCH_RECT_Y, UI_SWITCH_RECT_W,
                            UI_SWITCH_RECT_H, UI_SWITCH_RADIUS, u16SlotColor);
    UiDashboard_FillRounded(u16KnobX, (uint16_t)(UI_SWITCH_RECT_Y + UI_SWITCH_KNOB_INSET),
                            UI_SWITCH_KNOB_SIZE, UI_SWITCH_KNOB_SIZE, 1u, u16KnobColor);
}

/* ========================================================================== *
 *  7. 静态骨架
 * ========================================================================== */

/**
 * @brief  用 8px 弱化灰色绘制一个区域标签
 * @param[in] u16Bg 该区域底色（页面底色或卡片底色）
 */
static void UiDashboard_DrawLabel(uint16_t u16X, uint16_t u16BaselineY,
                                  uint16_t u16Bg, const char *pcText)
{
    UiFont_DrawText((int16_t)u16X, (int16_t)u16BaselineY, &g_tUiFontInter8,
                    UI_COLOR_TEXT_DIM, u16Bg, pcText);
}

/**
 * @brief  绘制只画一次的静态骨架
 * @note   整屏底色 + 顶部状态栏 + 两张卡片 + 三个平铺区域 + 全部标签。
 *         所有区域都铺到屏幕边缘（无外围留白）。
 *         一次性阻塞约 45ms（整屏填充占绝大部分），只在进入本页时执行。
 *         顶栏标题用 16px 字库：8px 在 1.14 寸屏上只有 1px 笔画，实际看不清。
 */
static void UiDashboard_DrawStatic(void)
{
    /* 1) 整屏背景（一次性阻塞填充，约 45ms @12MHz SPI） */
    LCD_FillRect(0u, 0u, UI_LAYOUT_W, UI_LAYOUT_H, UI_COLOR_PAGE_BG);

    /* 2) 顶部状态栏：绿色圆点 + 标题 + 分隔线 */
    UiDashboard_FillRounded(UI_BAR_DOT_X, UI_BAR_DOT_Y, UI_BAR_DOT_SIZE, UI_BAR_DOT_SIZE,
                            1u, UI_COLOR_VOUT_ON);
    UiFont_DrawText((int16_t)UI_BAR_TITLE_X, (int16_t)UI_BAR_BASELINE_Y,
                    &g_tUiFontInter16, UI_COLOR_TEXT_MAIN, UI_COLOR_PAGE_BG, "POWER MONITOR");
    LCD_FillRect(UI_BAR_LINE_X, UI_BAR_LINE_Y, UI_BAR_LINE_W, 1u, UI_COLOR_BORDER);

    /* 3) VBUS / VOUT 卡片 */
    UiDashboard_DrawCard(UI_VBUS_X, UI_CARD_Y, UI_CARD_W, UI_CARD_H);
    UiDashboard_DrawCard(UI_VOUT_X, UI_CARD_Y, UI_CARD_W, UI_CARD_H);
    UiDashboard_DrawLabel((uint16_t)(UI_VBUS_X + UI_CARD_PAD), UI_CARD_LABEL_BASE_Y,
                          UI_COLOR_CARD_BG, "VBUS");
    UiDashboard_DrawLabel((uint16_t)(UI_VOUT_X + UI_CARD_PAD), UI_CARD_LABEL_BASE_Y,
                          UI_COLOR_CARD_BG, "VOUT");

    /* 4) POWER / IBUS / OUTPUT 三个平铺区域 */
    LCD_FillRect(UI_POWER_X,  UI_TILE_Y, UI_POWER_W,  UI_TILE_H, UI_COLOR_CARD_BG);
    LCD_FillRect(UI_IBUS_X,   UI_TILE_Y, UI_IBUS_W,   UI_TILE_H, UI_COLOR_CARD_BG);
    LCD_FillRect(UI_OUTPUT_X, UI_TILE_Y, UI_OUTPUT_W, UI_TILE_H, UI_COLOR_CARD_BG);
    UiDashboard_DrawLabel(UI_POWER_VALUE_X,  UI_TILE_LABEL_BASE_Y, UI_COLOR_CARD_BG, "POWER");
    UiDashboard_DrawLabel(UI_IBUS_VALUE_X,   UI_TILE_LABEL_BASE_Y, UI_COLOR_CARD_BG, "IBUS");
    UiDashboard_DrawLabel(UI_OUTPUT_TEXT_X,  UI_TILE_LABEL_BASE_Y, UI_COLOR_CARD_BG, "OUTPUT");
}

/* ========================================================================== *
 *  8. 对外接口
 * ========================================================================== */

void UiDashboard_Init(void)
{
    memset(&s_tData, 0, sizeof(s_tData));
    memset(&s_tCache, 0, sizeof(s_tCache));
    s_bOutputDrawn = false;

    UiDashboard_DrawStatic();

    s_bReady = true;
}

void UiDashboard_SetData(const UiPowerData *ptData)
{
    if (ptData == 0)
    {
        return;
    }

    s_tData = *ptData;
}

void UiDashboard_Refresh(void)
{
    char     acText[8];
    uint16_t u16ValueX;
    bool     bLinkUp;

    if (s_bReady == false)
    {
        return;
    }

    /* ---- 顶部连接状态 ---- */
    bLinkUp = (s_tData.measurements_valid != false && s_tData.vbus_mv >= UI_VBUS_ONLINE_MV);
    if (s_tCache.bLinkUp != bLinkUp)
    {
        s_tCache.bLinkUp = bLinkUp;
        UiDashboard_DrawLinkStatus();
    }

    /* ---- VBUS（24px，青色） ---- */
    UiDashboard_FormatVoltage(acText, s_tData.vbus_mv, s_tData.measurements_valid);
    if (strcmp(acText, s_tCache.acVbus) != 0)
    {
        strcpy(s_tCache.acVbus, acText);
        u16ValueX = (uint16_t)(UI_VBUS_X + UI_CARD_PAD);
        UiDashboard_ClearRect(u16ValueX, UI_CARD_VALUE_RECT_Y,
                              UI_CARD_VALUE_RECT_W, UI_CARD_VALUE_RECT_H, UI_COLOR_CARD_BG);
        UiDashboard_DrawValueWithUnit(u16ValueX, UI_CARD_VALUE_BASE_Y,
                                      &g_tUiFontInter24, acText, UI_COLOR_VBUS, "V");
    }

    /* ---- VOUT（24px，绿色） ---- */
    UiDashboard_FormatVoltage(acText, s_tData.vout_mv, s_tData.measurements_valid);
    if (strcmp(acText, s_tCache.acVout) != 0)
    {
        strcpy(s_tCache.acVout, acText);
        u16ValueX = (uint16_t)(UI_VOUT_X + UI_CARD_PAD);
        UiDashboard_ClearRect(u16ValueX, UI_CARD_VALUE_RECT_Y,
                              UI_CARD_VALUE_RECT_W, UI_CARD_VALUE_RECT_H, UI_COLOR_CARD_BG);
        UiDashboard_DrawValueWithUnit(u16ValueX, UI_CARD_VALUE_BASE_Y,
                                      &g_tUiFontInter24, acText, UI_COLOR_VOUT_ON, "V");
    }

    /* ---- POWER（16px，橙色；输入功率 = VBUS x IBUS） ---- */
    UiDashboard_FormatPower(acText, s_tData.power_mw, s_tData.measurements_valid);
    if (strcmp(acText, s_tCache.acPower) != 0)
    {
        strcpy(s_tCache.acPower, acText);
        UiDashboard_ClearRect(UI_POWER_VALUE_X, UI_TILE_VALUE_RECT_Y,
                              UI_POWER_VALUE_RECT_W, UI_TILE_VALUE_RECT_H, UI_COLOR_CARD_BG);
        UiDashboard_DrawValueWithUnit(UI_POWER_VALUE_X, UI_TILE_VALUE_BASE_Y,
                                      &g_tUiFontInter16, acText, UI_COLOR_POWER, "W");
    }

    /* ---- IBUS（16px，主文字色） ---- */
    UiDashboard_FormatCurrent(acText, s_tData.ibus_ma, s_tData.measurements_valid);
    if (strcmp(acText, s_tCache.acIbus) != 0)
    {
        strcpy(s_tCache.acIbus, acText);
        UiDashboard_ClearRect(UI_IBUS_VALUE_X, UI_TILE_VALUE_RECT_Y,
                              UI_IBUS_VALUE_RECT_W, UI_TILE_VALUE_RECT_H, UI_COLOR_CARD_BG);
        UiDashboard_DrawValueWithUnit(UI_IBUS_VALUE_X, UI_TILE_VALUE_BASE_Y,
                                      &g_tUiFontInter16, acText, UI_COLOR_TEXT_MAIN, "A");
    }

    /* ---- OUTPUT 开关状态 ---- */
    if ((s_bOutputDrawn == false) || (s_tCache.bOutputEnabled != s_tData.output_enabled))
    {
        s_tCache.bOutputEnabled = s_tData.output_enabled;
        s_bOutputDrawn = true;
        UiDashboard_DrawOutputState();
    }
}
