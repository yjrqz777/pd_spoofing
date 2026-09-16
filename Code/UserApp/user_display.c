/**
 * @file    user_display.c
 * @brief   用户显示任务实现 — 单页实时数据仪表界面
 *******************************************************************************
 * @note    屏幕：240x135 横屏（ST7789V，SPI1 + DMA）
 *
 *          界面实现见 Code/UserApp/ui/ui_dashboard.c：
 *            +--------------------------------------------+  <- 内容直接贴屏幕四边
 *            | * POWER MONITOR                  ONLINE     |  顶部状态栏（16px）
 *            |--------------------------------------------|
 *            | +------------------+ +-------------------+ |
 *            | | VBUS     12.08 V | | VOUT      11.96 V | |  卡片
 *            | +------------------+ +-------------------+ |
 *            | +---------+ +--------+ +-----------------+ |
 *            | | POWER   | | IBUS   | | OUTPUT     [ o] | |  小区域
 *            | | 14.7 W  | | 1.22 A | | ON              | |
 *            | +---------+ +--------+ +-----------------+ |
 *
 *          刷新策略：
 *            - ADC 采样每 100ms 一次（BspAdcUpdateAll，含 IIR 滤波）；
 *            - 界面每 200ms（5Hz）提交一帧数据，UiDashboard_Refresh() 比较
 *              格式化后的字符串，只重绘变化的矩形；
 *            - 整屏背景只在进入 RUNNING 时绘制一次。
 *******************************************************************************
 */

#include "user_display.h"
#include "user_config.h"
#include "ui/ui_dashboard.h"
#include "bsp_lcd.h"
#include "bsp_adc.h"
#include "bsp_board.h"
#include "bsp_button.h"
#include "bsp_usb_pd.h"
#include "bsp_spi.h"
#include "st7789v/st7789v.h"

/** @brief Runtime diagnostic log interval in milliseconds. */
#define DISPLAY_DIAGNOSTIC_INTERVAL_MS (1000u)

/** @brief 进入 RUNNING 前，整屏只填充一次 */
static uint8_t s_u8ScreenCleared = 0u;

/** @brief 顶栏是否已经画过（静态内容只画一次，避免每帧重复输出） */
static uint8_t s_u8StaticDrawn = 0u;

/** @brief 采样与刷新的时间累加器（单位 ms） */
static uint16_t s_u16SampleAcc = 0u;
static uint16_t s_u16RefreshAcc = 0u;
static uint16_t s_u16DiagnosticAcc = 0u;

/**
 * @brief  初始化显示模块
 * @note   初始化 LCD、SPI1 与 TX DMA。包含初始化延时，仅上电时执行一次。
 */
void UsrDisplayInit(void)
{
#if LCD_IO_STATIC_TEST_ENABLE
    printf("[DISPLAY] LCD IO static-level test active; controller init disabled\r\n");
#else
    printf("[DISPLAY] initialization begin\r\n");
    BspLcdInit();
    printf("[DISPLAY] initialization end, spi_error=%u\r\n",
           (unsigned int)BspSpiHasError());
#endif

    s_u8ScreenCleared = 0u;
    s_u8StaticDrawn   = 0u;
    s_u16SampleAcc    = 0u;
    s_u16RefreshAcc   = 0u;
    s_u16DiagnosticAcc = 0u;
}

/**
 * @brief  把 ADC 的浮点物理量换算成界面层使用的整数工程单位
 * @param[in] f32Value 物理量（V 或 A）
 * @return 毫单位整数（mV 或 mA）；负值按 0 处理
 * @note   只是定点换算，不涉及浮点格式化；界面层完全不碰浮点。
 */
static uint32_t UsrDisplayToMilli(float f32Value)
{
    if (f32Value <= 0.0f)
    {
        return 0u;
    }

    return (uint32_t)(f32Value * 1000.0f + 0.5f);
}

/**
 * @brief  把最近一次采样组装成界面数据
 * @param[out] ptUi  界面数据结构
 */
static void UsrDisplayBuildUiData(UiPowerData *ptUi)
{
    const tBspAdcDataDef *ptData = BspAdcGetData();

    ptUi->vbus_mv = UsrDisplayToMilli(ptData->f32Voltage);
    ptUi->vout_mv = UsrDisplayToMilli(ptData->f32Vout);
    ptUi->ibus_ma = UsrDisplayToMilli(ptData->f32Current);

    /* 输入功率 = VBUS x IBUS（64 位中间值防止溢出，再四舍五入到 mW）。
     * 当前硬件没有 IOUT 采样，不能把它当成"输出功率"。 */
    ptUi->power_mw = (uint32_t)(((uint64_t)ptUi->vbus_mv * (uint64_t)ptUi->ibus_ma + 500u) / 1000u);

    /* 开关状态取软件维护的 VOUT-EN 命令状态，而不是 ADC 推断 */
    ptUi->output_enabled = (BspBoardGetVoutEnable() != 0u) ? true : false;

    ptUi->measurements_valid = (ptData->u8Valid != 0u) ? true : false;
}

/**
 * @brief  INIT 状态显示处理（上电初期，屏幕尚未初始化完成）
 */
static void UsrDisplayInitState(void)
{
    /* INIT 仅持续约 100ms，此阶段不放任何绘制，避免与 LCD 初始化竞争 */
}

/**
 * @brief  POWER_ON 状态显示处理：开机页
 * @note   清屏与字符串都是"入队 + 分批 DMA"，本函数只登记一次内容，
 *         实际像素由 BspLcdService() 在每个时间片推进，不再阻塞主循环。
 */
static void UsrDisplayPowerOnState(void)
{
    if (s_u8ScreenCleared == 0u)
    {
        BspLcdClearScreen(WHITE);
        s_u8ScreenCleared = 1u;
    }

    if (s_u8StaticDrawn == 0u)
    {
        BspLcdShowString(40u,  30u, "pd-spoofing", RED,   WHITE, 24u, 0u);
        BspLcdShowString(40u,  66u, "CH32X035G8U", BLUE,  WHITE, 16u, 0u);
        BspLcdShowString(40u,  92u, "FW  V0.1",    BLACK, WHITE, 16u, 0u);
        s_u8StaticDrawn = 1u;
    }
}

/**
 * @brief  RUNNING 状态显示处理：实时数据仪表界面
 * @note   采样每 USR_DISPLAY_SAMPLE_MS 一次；
 *         界面每 USR_DISPLAY_REFRESH_MS 提交一帧并按需局部重绘。
 *         仪表页自己直接操作 LCD（阻塞小矩形 DMA），不走字段队列，
 *         每次重绘量都很小，不会长时间占用主循环。
 */
static void UsrDisplayRunningState(void)
{
    UiPowerData tUiData;

    /* 进入 RUNNING 的第一次：整屏背景 + 静态骨架只画一次 */
    if (s_u8ScreenCleared == 0u)
    {
        /* 丢弃开机页可能还在排队的字段帧，避免和仪表页的直接绘制抢 DMA */
        BspLcdCancelRefresh();
        UiDashboard_Init();
        s_u8ScreenCleared = 1u;
        s_u8StaticDrawn   = 1u;
        s_u16SampleAcc    = USR_DISPLAY_SAMPLE_MS;
        s_u16RefreshAcc   = USR_DISPLAY_REFRESH_MS;
    }

    /* 采样累加 */
    s_u16SampleAcc += USR_DISPLAY_TASK_INTERVAL_MS;
    if (s_u16SampleAcc >= USR_DISPLAY_SAMPLE_MS)
    {
        s_u16SampleAcc = 0u;
        BspAdcUpdateAll();
    }

    /* 组装一次数据快照（纯整数换算，开销可忽略；也是诊断打印的数据来源） */
    UsrDisplayBuildUiData(&tUiData);

    /* 刷新累加：周期到时提交一帧数据，由界面层比较后局部重绘 */
    s_u16RefreshAcc += USR_DISPLAY_TASK_INTERVAL_MS;
    if (s_u16RefreshAcc >= USR_DISPLAY_REFRESH_MS)
    {
        s_u16RefreshAcc = 0u;

        UiDashboard_SetData(&tUiData);
        UiDashboard_Refresh();
    }

    s_u16DiagnosticAcc += USR_DISPLAY_TASK_INTERVAL_MS;
    if (s_u16DiagnosticAcc >= DISPLAY_DIAGNOSTIC_INTERVAL_MS)
    {
        const tBspAdcDataDef *ptData = BspAdcGetData();
        const tBspUsbPdStatusDef *ptPdStatus = BspUsbPdGetStatus();

        s_u16DiagnosticAcc = 0u;

        printf("[RUN] rail vbus=%u vout=%u ibus=%u power=%u valid=%u EN=%u\r\n",
               (unsigned int)tUiData.vbus_mv,
               (unsigned int)tUiData.vout_mv,
               (unsigned int)tUiData.ibus_ma,
               (unsigned int)tUiData.power_mw,
               (unsigned int)(ptData->u8Valid),
               (unsigned int)BspBoardGetVoutEnable());
        printf("[RUN] raw vbus=%u vout=%u ibus=%u keys=0x%02x PD=%u/%u %umV dma_idle=%u spi_error=%u\r\n",
               (unsigned int)ptData->u16Raw[E_BSP_ADC_VBUS],
               (unsigned int)ptData->u16Raw[E_BSP_ADC_VOUT],
               (unsigned int)ptData->u16Raw[E_BSP_ADC_IBUS],
               (unsigned int)BspButtonGetRawMask(),
               (unsigned int)ptPdStatus->u8RequestedPdo,
               (unsigned int)ptPdStatus->u8PdoCount,
               (unsigned int)ptPdStatus->u16VoltageMv,
               (unsigned int)BspSpiIsIdle(),
               (unsigned int)BspSpiHasError());
    }
}

/**
 * @brief  OFF 状态显示处理（预留）
 */
static void UsrDisplayOffState(void)
{
    s_u8ScreenCleared = 0u;
    s_u8StaticDrawn   = 0u;
}

/**
 * @brief  Protothread 显示协程任务
 * @return PT 状态码
 * @note   首次进入执行初始化，之后每 USR_DISPLAY_TASK_INTERVAL_MS
 *         按当前系统状态刷新显示，并推进 LCD 字段状态机。
 */
uint16_t UsrDisplayTask(void)
{
#if !USER_LCD_ENABLE
    /* LCD 已关闭：不初始化 ST7789V、不刷屏、不提供 [RUN] 诊断
     * （由 UsrPdTask / user_time.c 输出）。 */
    return PT_ENDED;
#else
    int8_t i = 0;
    static const struct
    {
        eSysStateDef eState;
        void (*pfFunction)(void);
    } atDisplayFunction[E_SYS_STATE_MAX] =
    {
        {E_SYS_STATE_INIT,     UsrDisplayInitState},
        {E_SYS_STATE_POWER_ON, UsrDisplayPowerOnState},
        {E_SYS_STATE_OFF,      UsrDisplayOffState},
        {E_SYS_STATE_RUNNING,  UsrDisplayRunningState},
    };

    PT_BEGIN()
    {
        UsrDisplayInit();
    }

    while (1)
    {
        PT_WAIT_UNTIL(USR_DISPLAY_TASK_INTERVAL_MS / OS_TICK_MS);

#if LCD_IO_STATIC_TEST_ENABLE
        /* Keep the LCD bus untouched while probing the five signals. */
        continue;
#endif

        /* 状态改变时复位"静态内容已画"标志，强制重画界面骨架 */
        {
            static eSysStateDef eLastState = E_SYS_STATE_MAX;

            if (eLastState != tSysData.eState)
            {
                printf("[DISPLAY] state=%u\r\n", (unsigned int)tSysData.eState);
                if (eLastState != E_SYS_STATE_MAX)
                {
                    s_u8ScreenCleared = 0u;
                    s_u8StaticDrawn = 0u;
                }
                eLastState = tSysData.eState;
            }
        }

        for (i = (int8_t)(sizeof(atDisplayFunction) / sizeof(atDisplayFunction[0])) - 1; i >= 0; i--)
        {
            if (tSysData.eState == atDisplayFunction[i].eState)
            {
                atDisplayFunction[i].pfFunction();
                break;
            }
        }

        /* 推进 LCD 字段状态机（开机页的异步字段；仪表页为无操作） */
        BspLcdService((uint8_t)tSysData.eState);
    }
    PT_END();
#endif /* USER_LCD_ENABLE */
}
