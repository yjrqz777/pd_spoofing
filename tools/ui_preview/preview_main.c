/**
 * @file    preview_main.c
 * @brief   PC 端界面预览与断言（无硬件时的验证入口）
 *******************************************************************************
 * @note    编译（MinGW gcc）：
 *              gcc -std=gnu99 -Wall -Wextra -I stub -I ../../Code/UserApp/ui \
 *                  preview_main.c lcd_sim.c ../../Code/UserApp/ui/ui_font.c \
 *                  ../../Code/UserApp/ui/ui_dashboard.c -o ui_preview.exe
 *
 *          验证内容：
 *            1) 数值字距稳定性：所有 "xx.xx" / "x.xx" / "xx.x" 组合宽度一致；
 *            2) 布局约束：每个区域里除"允许的子矩形"外，其余像素必须还是底色，
 *               也就是不允许内容越出设计矩形；
 *            3) 导出若干场景的 PPM（再由脚本转 PNG）供人工核对排版。
 *******************************************************************************
 */

#include "st7789v/st7789v.h"
#include "ui_font.h"
#include "ui_dashboard.h"
#include "fonts/font_inter_8.h"
#include "fonts/font_inter_16.h"
#include "fonts/font_inter_24.h"

#include <stdio.h>
#include <string.h>

/* 调色板（与 ui_dashboard.c 保持一致，仅用于断言） */
#define COLOR_PAGE_BG   ((uint16_t)0x0041u)
#define COLOR_CARD_BG   ((uint16_t)0x0883u)
#define COLOR_BORDER    ((uint16_t)0x1946u)
#define COLOR_ON        ((uint16_t)0x6733u)
#define COLOR_DIM       ((uint16_t)0x7411u)

void     LcdSim_GetStats(uint32_t *pu32Rects, uint32_t *pu32Pixels);
void     LcdSim_ResetStats(void);
int      LcdSim_SavePpm(const char *pcPath);
uint16_t LcdSim_GetPixel(uint16_t u16X, uint16_t u16Y);

typedef struct
{
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
} Rect;

static int s_iFailures = 0;

static int PointInRect(uint16_t x, uint16_t y, Rect r)
{
    return (x >= r.x) && (x < (uint16_t)(r.x + r.w)) &&
           (y >= r.y) && (y < (uint16_t)(r.y + r.h));
}

/** @brief 断言：区域内除 allowed 之外的所有像素都等于底色 */
static void CheckRegionBackground(const char *pName, Rect region, uint16_t u16Bg,
                                  const Rect *pAllowed, int iAllowedCount)
{
    uint16_t x;
    uint16_t y;
    int      iFail = 0;

    for (y = region.y; y < (uint16_t)(region.y + region.h); y++)
    {
        for (x = region.x; x < (uint16_t)(region.x + region.w); x++)
        {
            int k;

            for (k = 0; k < iAllowedCount; k++)
            {
                if (PointInRect(x, y, pAllowed[k]))
                {
                    break;
                }
            }
            if (k < iAllowedCount)
            {
                continue;
            }

            if (LcdSim_GetPixel(x, y) != u16Bg)
            {
                if (iFail == 0)
                {
                    printf("    [FAIL] %s: (%u,%u) = 0x%04X, expected 0x%04X\n",
                           pName, (unsigned)x, (unsigned)y,
                           (unsigned)LcdSim_GetPixel(x, y), (unsigned)u16Bg);
                }
                iFail++;
            }
        }
    }

    if (iFail == 0)
    {
        printf("    [ OK ] %s: 内容未越出设计矩形\n", pName);
    }
    else
    {
        s_iFailures++;
    }
}

/** @brief 断言：矩形范围内存在指定颜色（用于确认确实画上去了） */
static void CheckRegionContains(const char *pName, Rect region, uint16_t u16Color)
{
    uint16_t x;
    uint16_t y;

    for (y = region.y; y < (uint16_t)(region.y + region.h); y++)
    {
        for (x = region.x; x < (uint16_t)(region.x + region.w); x++)
        {
            if (LcdSim_GetPixel(x, y) == u16Color)
            {
                printf("    [ OK ] %s: 找到期望颜色 0x%04X\n", pName, (unsigned)u16Color);
                return;
            }
        }
    }

    printf("    [FAIL] %s: 未找到期望颜色 0x%04X\n", pName, (unsigned)u16Color);
    s_iFailures++;
}

/** @brief 字库覆盖检查：界面里出现的每个字符都必须能在对应字库里找到 */
static void CheckStringCovered(const char *pName, const UiFont *pFont, const char *pText)
{
    const char *p;

    for (p = pText; *p != '\0'; p++)
    {
        if (UiFont_FindGlyph(pFont, (uint8_t)*p) == 0)
        {
            printf("    [FAIL] %s: 字库缺少字符 '%c' (0x%02X)，界面会静默丢字\n",
                   pName, *p, (unsigned)(uint8_t)*p);
            s_iFailures++;
            return;
        }
    }
    printf("    [ OK ] %-18s \"%s\"\n", pName, pText);
}

static void TestGlyphCoverage(void)
{
    printf("[2] 字库覆盖（界面用到的字符串）\n");

    CheckStringCovered("24px 电压", &g_tUiFontInter24, "0123456789.-");
    CheckStringCovered("16px 电流/功率", &g_tUiFontInter16, "0123456789.-");
    CheckStringCovered("16px 开关文字", &g_tUiFontInter16, "ON");
    CheckStringCovered("16px 开关文字", &g_tUiFontInter16, "OFF");

    CheckStringCovered("16px 顶栏标题", &g_tUiFontInter16, "POWER MONITOR");
    CheckStringCovered("16px 连接状态", &g_tUiFontInter16, "ONLINE");
    CheckStringCovered("8px 卡片标签", &g_tUiFontInter8, "VBUS");
    CheckStringCovered("8px 卡片标签", &g_tUiFontInter8, "VOUT");
    CheckStringCovered("8px 区域标签", &g_tUiFontInter8, "POWER");
    CheckStringCovered("8px 区域标签", &g_tUiFontInter8, "IBUS");
    CheckStringCovered("8px 区域标签", &g_tUiFontInter8, "OUTPUT");
    CheckStringCovered("8px 单位", &g_tUiFontInter8, "VAW");
}

/* --------------------------------------------------------------------------- */

/** @brief 数值宽度一致性：同一格式下所有取值宽度必须相同，否则会左右跳动 */
static void TestMetricStability(void)
{
    uint32_t v;

    printf("[1] 数值宽度一致性\n");

    for (v = 0; v <= 99999u; v += 137u)
    {
        char a[8];
        char b[8];

        (void)snprintf(a, sizeof(a), "%02u.%02u", (unsigned)(v / 1000u), (unsigned)((v % 1000u) / 10u));
        (void)snprintf(b, sizeof(b), "00.00");
        if (UiFont_MeasureText(&g_tUiFontInter24, a) != UiFont_MeasureText(&g_tUiFontInter24, b))
        {
            printf("    [FAIL] 24px 电压宽度不一致: %s\n", a);
            s_iFailures++;
            return;
        }
    }
    printf("    [ OK ] 24px 电压 \"xx.xx\" 宽度恒为 %u px\n",
           (unsigned)UiFont_MeasureText(&g_tUiFontInter24, "00.00"));

    for (v = 0; v <= 9999u; v += 41u)
    {
        char a[8];

        (void)snprintf(a, sizeof(a), "%u.%02u", (unsigned)(v / 1000u), (unsigned)((v % 1000u) / 10u));
        if (UiFont_MeasureText(&g_tUiFontInter16, a) != UiFont_MeasureText(&g_tUiFontInter16, "0.00"))
        {
            printf("    [FAIL] 16px 电流宽度不一致: %s\n", a);
            s_iFailures++;
            return;
        }
    }
    printf("    [ OK ] 16px 电流 \"x.xx\" 宽度恒为 %u px\n",
           (unsigned)UiFont_MeasureText(&g_tUiFontInter16, "0.00"));

    for (v = 0; v <= 999u; v += 7u)
    {
        char a[8];

        (void)snprintf(a, sizeof(a), "%02u.%u", (unsigned)(v / 10u), (unsigned)(v % 10u));
        if (UiFont_MeasureText(&g_tUiFontInter16, a) != UiFont_MeasureText(&g_tUiFontInter16, "00.0"))
        {
            printf("    [FAIL] 16px 功率宽度不一致: %s\n", a);
            s_iFailures++;
            return;
        }
    }
    printf("    [ OK ] 16px 功率 \"xx.x\" 宽度恒为 %u px\n",
           (unsigned)UiFont_MeasureText(&g_tUiFontInter16, "00.0"));

    printf("    [INFO] 8px 单位宽度 V=%u A=%u W=%u\n",
           (unsigned)UiFont_MeasureText(&g_tUiFontInter8, "V"),
           (unsigned)UiFont_MeasureText(&g_tUiFontInter8, "A"),
           (unsigned)UiFont_MeasureText(&g_tUiFontInter8, "W"));
}

/** @brief 布局约束检查（在渲染完一个场景之后调用） */
static void TestLayout(void)
{
    static const Rect vbusCard = {0u, 28u, 118u, 54u};
    static const Rect voutCard = {122u, 28u, 118u, 54u};
    static const Rect powerTile = {0u, 87u, 77u, 48u};
    static const Rect ibusTile = {81u, 87u, 77u, 48u};
    static const Rect outputTile = {162u, 87u, 78u, 48u};

    static const Rect vbusAllowed[] = {{10u, 36u, 40u, 14u}, {10u, 51u, 88u, 24u}};
    static const Rect voutAllowed[] = {{132u, 36u, 40u, 14u}, {132u, 51u, 88u, 24u}};
    static const Rect powerAllowed[] = {{8u, 94u, 40u, 10u}, {8u, 113u, 60u, 16u}};
    static const Rect ibusAllowed[] = {{89u, 94u, 40u, 10u}, {89u, 113u, 50u, 16u}};
    static const Rect outputAllowed[] = {{170u, 94u, 40u, 10u}, {170u, 113u, 32u, 16u},
                                         {213u, 114u, 18u, 10u}};

    printf("[3] 布局约束（区域内除设计矩形外必须仍是底色）\n");

    CheckRegionBackground("VBUS 卡片", (Rect){vbusCard.x + 1u, vbusCard.y + 1u,
                                              (uint16_t)(vbusCard.w - 2u), (uint16_t)(vbusCard.h - 2u)},
                          COLOR_CARD_BG, vbusAllowed, 2);
    CheckRegionBackground("VOUT 卡片", (Rect){voutCard.x + 1u, voutCard.y + 1u,
                                              (uint16_t)(voutCard.w - 2u), (uint16_t)(voutCard.h - 2u)},
                          COLOR_CARD_BG, voutAllowed, 2);
    CheckRegionBackground("POWER 区域", (Rect){powerTile.x, powerTile.y, powerTile.w, powerTile.h},
                          COLOR_CARD_BG, powerAllowed, 2);
    CheckRegionBackground("IBUS 区域", (Rect){ibusTile.x, ibusTile.y, ibusTile.w, ibusTile.h},
                          COLOR_CARD_BG, ibusAllowed, 2);
    CheckRegionBackground("OUTPUT 区域", (Rect){outputTile.x, outputTile.y, outputTile.w, outputTile.h},
                          COLOR_CARD_BG, outputAllowed, 3);

    /* 顶部状态栏（y=23 的分隔线单独检查，不纳入本区域） */
    {
        static const Rect barAllowed[] = {{6u, 9u, 6u, 6u}, {18u, 4u, 142u, 16u},
                                          {158u, 4u, 78u, 19u}};
        CheckRegionBackground("顶部状态栏", (Rect){0u, 0u, 240u, 23u}, COLOR_PAGE_BG, barAllowed, 3);
    }

    /* 分隔线 */
    {
        int x;
        int ok = 1;

        for (x = 0; x < 240; x++)
        {
            if (LcdSim_GetPixel((uint16_t)x, 23u) != COLOR_BORDER)
            {
                ok = 0;
                break;
            }
        }
        printf("    [%s] 分隔线 y=23: %s\n", ok ? " OK " : "FAIL", ok ? "整条 1px 边框色" : "有缺口");
        if (!ok)
        {
            s_iFailures++;
        }
    }

    /* 卡片边框 */
    {
        int ok = (LcdSim_GetPixel(0u, 28u) == COLOR_BORDER) &&
                 (LcdSim_GetPixel(117u, 81u) == COLOR_BORDER) &&
                 (LcdSim_GetPixel(0u, 81u) == COLOR_BORDER) &&
                 (LcdSim_GetPixel(117u, 28u) == COLOR_BORDER);

        printf("    [%s] VBUS 卡片四角为边框色\n", ok ? " OK " : "FAIL");
        if (!ok)
        {
            s_iFailures++;
        }
    }

    CheckRegionContains("VBUS 数值", (Rect){10u, 51u, 88u, 24u}, 0x46DCu);
    CheckRegionContains("OUTPUT 绿色滑块", (Rect){213u, 114u, 18u, 10u}, COLOR_ON);
}

/* --------------------------------------------------------------------------- */

static void RenderScenario(const char *pName, const UiPowerData *pData)
{
    uint32_t u32Rects = 0u;
    uint32_t u32Pixels = 0u;
    char     acPath[256];

    UiDashboard_Init();

    LcdSim_ResetStats();
    UiDashboard_SetData(pData);
    UiDashboard_Refresh();
    LcdSim_GetStats(&u32Rects, &u32Pixels);

    printf("[%s] 首帧刷新: %u 个矩形, %u 像素 (%.1f KB SPI)\n",
           pName, (unsigned)u32Rects, (unsigned)u32Pixels, (double)u32Pixels * 2.0 / 1024.0);

    (void)snprintf(acPath, sizeof(acPath), "out/%s.ppm", pName);
    if (LcdSim_SavePpm(acPath) != 0)
    {
        printf("    [FAIL] 无法写出 %s\n", acPath);
        s_iFailures++;
    }

    /* 第二帧提交完全相同的数据：应该一次矩形都不用刷 */
    LcdSim_ResetStats();
    UiDashboard_SetData(pData);
    UiDashboard_Refresh();
    LcdSim_GetStats(&u32Rects, &u32Pixels);
    printf("    [%s] 数据未变时的重复刷新: %u 个矩形, %u 像素\n",
           (u32Rects == 0u) ? " OK " : "FAIL", (unsigned)u32Rects, (unsigned)u32Pixels);
    if (u32Rects != 0u)
    {
        s_iFailures++;
    }
}

/** @brief 稳态刷新量：只改一路数据时，单次刷新应该只重绘一个矩形 */
static void TestIncrementalCost(void)
{
    UiPowerData tBase;
    UiPowerData tNext;
    uint32_t    u32Rects = 0u;
    uint32_t    u32Pixels = 0u;

    printf("[5] 稳态刷新量（只改一路数据）\n");

    memset(&tBase, 0, sizeof(tBase));
    tBase.vbus_mv = 20080u;
    tBase.vout_mv = 19960u;
    tBase.ibus_ma = 1220u;
    tBase.power_mw = 24498u;
    tBase.output_enabled = true;
    tBase.measurements_valid = true;

    UiDashboard_Init();
    UiDashboard_SetData(&tBase);
    UiDashboard_Refresh();

    /* 只把 IBUS 从 1.22A 改到 1.21A */
    tNext = tBase;
    tNext.ibus_ma = 1210u;
    tNext.power_mw = 24303u;
    LcdSim_ResetStats();
    UiDashboard_SetData(&tNext);
    UiDashboard_Refresh();
    LcdSim_GetStats(&u32Rects, &u32Pixels);
    printf("    IBUS+POWER 各变一档: %u 个矩形, %u 像素 (%.1f KB SPI)\n",
           (unsigned)u32Rects, (unsigned)u32Pixels, (double)u32Pixels * 2.0 / 1024.0);

    /* 只切换 VOUT-EN */
    tNext = tBase;
    tNext.output_enabled = false;
    LcdSim_ResetStats();
    UiDashboard_SetData(&tNext);
    UiDashboard_Refresh();
    LcdSim_GetStats(&u32Rects, &u32Pixels);
    printf("    ON->OFF 开关翻转    : %u 个矩形, %u 像素 (%.1f KB SPI)\n",
           (unsigned)u32Rects, (unsigned)u32Pixels, (double)u32Pixels * 2.0 / 1024.0);
}

int main(void)
{
    UiPowerData tData;

    printf("=== ui_dashboard PC 预览/断言 ===\n\n");

    TestMetricStability();
    printf("\n");

    TestGlyphCoverage();
    printf("\n");

    /* 场景 1：典型 PD 取电（20V / 2.5A） */
    memset(&tData, 0, sizeof(tData));
    tData.vbus_mv = 20080u;
    tData.vout_mv = 19960u;
    tData.ibus_ma = 1220u;
    tData.power_mw = (uint32_t)(((unsigned long long)tData.vbus_mv * tData.ibus_ma + 500u) / 1000u);
    tData.output_enabled = true;
    tData.measurements_valid = true;
    RenderScenario("01_running_on", &tData);

    printf("\n[4] 布局约束检查（场景 1 的渲染帧）\n");
    TestLayout();

    /* 场景 2：输出关断 */
    tData.output_enabled = false;
    tData.ibus_ma = 0u;
    tData.power_mw = 0u;
    RenderScenario("02_running_off", &tData);

    /* 场景 3：采样无效 */
    memset(&tData, 0, sizeof(tData));
    RenderScenario("03_invalid", &tData);

    /* 场景 4：最大值（用于检查位数饱和时的排版） */
    memset(&tData, 0, sizeof(tData));
    tData.vbus_mv = 99999u;
    tData.vout_mv = 99999u;
    tData.ibus_ma = 9999u;
    tData.power_mw = 99999u;
    tData.output_enabled = true;
    tData.measurements_valid = true;
    RenderScenario("04_max", &tData);

    printf("\n");
    TestIncrementalCost();

    printf("\n=== 结果: %s (失败项 %d) ===\n", (s_iFailures == 0) ? "全部通过" : "存在失败", s_iFailures);
    return (s_iFailures == 0) ? 0 : 1;
}
