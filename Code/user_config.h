/**
 * @file user_config.h
 * @brief   全局配置参数头文件
 * @note    用户可在此文件中定义项目级配置宏（如控制参数、功能开关等）
 *******************************************************************************
 */

#ifndef __USER_CONFIG_H__
#define __USER_CONFIG_H__

/* ========================================================================== *
 *  2. 全局类型别名
 * ========================================================================== */

typedef enum
{
    E_OK = 0,        /**< 成功 */
    E_ERROR,         /**< 失败 */
    E_BUSY           /**< 忙 */
} eStatusDef;

/* ========================================================================== *
 *  3. 系统时基
 * ========================================================================== */

/** @brief 系统时间片节拍周期（毫秒），对应 TIM3 更新中断周期 */
#define PD_TICK_MS (1u)

/** @brief 系统主频（Hz），由 system_ch32x035.c 的 SetSysClockTo48_HSI() 设定 */
#define BOARD_SYSCLK_HZ (48000000u)

/* ========================================================================== *
 *  4. 引脚映射（唯一真相来源）
 *  ---------------------------------------------------------------------------
 *  依据：doc/schematic/NETLIST.md
 *  MCU: CH32X035G8U6 (QFN-28)
 * ========================================================================== */

/* ---- 4.1 LCD (ST7789V, SPI1 Mode 2 + DMA1 Channel 3) ---- *
 * NETLIST: LCD_SCK=PA5，LCD_SDA=PA7，
 *          LCD_CS=PA3, LCD_DC=PA2, LCD_RES=PA1
 * 注意：本板 LCD 背光 LEDK 直接接地（硬件常亮），无背光控制引脚。 */
#define LCD_RES_PORT        GPIOA
#define LCD_RES_PIN         GPIO_Pin_1

#define LCD_DC_PORT         GPIOA
#define LCD_DC_PIN          GPIO_Pin_2

#define LCD_CS_PORT         GPIOA
#define LCD_CS_PIN          GPIO_Pin_3

#define LCD_SCK_PORT        GPIOA
#define LCD_SCK_PIN         GPIO_Pin_5

#define LCD_SDA_PORT        GPIOA
#define LCD_SDA_PIN         GPIO_Pin_7

/* ---- 4.2 按键 (低电平有效, 外部 10k 上拉 + 10nF 消抖) ---- */
#define KEY1_PORT           GPIOB
#define KEY1_PIN            GPIO_Pin_3     /* KEY-1 -> MCU PB3  */

#define KEY2_PORT           GPIOB
#define KEY2_PIN            GPIO_Pin_4     /* KEY-2 -> MCU PB4  */

#define KEY3_PORT           GPIOB
#define KEY3_PIN            GPIO_Pin_6     /* KEY-3 -> MCU PB6  */

/** @brief 本板实际按键数量（原理图 SW1/SW2/SW3 = 3 个） */
#define BOARD_KEY_NUM       (3u)

/* ---- 4.3 ADC 采样 (12bit, ADC1) ---- *
 * NETLIST: IBUS-ADC=PA4(ADC_IN4), USB-VBUS-ADC=PC0(ADC_IN10),
 *          VOUT-ADC=PA0(ADC_IN0)
 * 依据：数据手册 2.3 节复用表 —— 引脚名后缀 Axx 即 ADC_INxx。 */
#define ADC_IBUS_PORT       GPIOA
#define ADC_IBUS_PIN        GPIO_Pin_4
#define ADC_IBUS_CHANNEL    ADC_Channel_4      /* PA4  A4  */

#define ADC_VBUS_PORT       GPIOC
#define ADC_VBUS_PIN        GPIO_Pin_0
#define ADC_VBUS_CHANNEL    ADC_Channel_10     /* PC0  A10 */

#define ADC_VOUT_PORT       GPIOA
#define ADC_VOUT_PIN        GPIO_Pin_0
#define ADC_VOUT_CHANNEL    ADC_Channel_0      /* PA0  A0  */

/* ---- 4.4 输出使能 VOUT-EN ---- *
 * NETLIST: MCU PB12 -> R17 1k -> Q3 2N7002 栅极 -> R15 10k -> Q1/Q2 栅极
 * 逻辑：PB12 输出高 -> Q3 导通 -> P-MOS 栅极拉低 -> VOUT 导通。 */
#define VOUT_EN_PORT        GPIOB
#define VOUT_EN_PIN         GPIO_Pin_12

/* ---- 4.5 指示灯 LED ---- *
 * NETLIST: MCU PB8 -> LED1 -> R37 100R -> GND（高电平点亮） */
#define LED_RUN_PORT        GPIOB
#define LED_RUN_PIN         GPIO_Pin_8

/* ---- 4.6 WS2812（TIM1_CH1 PWM + DMA1 Channel 5） ---- */
#define WS2812_PORT          GPIOB
#define WS2812_PIN           GPIO_Pin_9

/* ---- 4.7 蜂鸣器 BEEP ---- *
 * NETLIST: R24 1k -> Q4 S8050 基极（高电平鸣响） */
#define BEEP_PORT           GPIOB
#define BEEP_PIN            GPIO_Pin_5     /* 预留：原理图未标注 MCU 侧网络，需硬件确认 */

/* ---- 4.8 日志串口 USART1 ---- *
 * NETLIST: LOG-TX = PB10 -> R34 100R -> H2.2；LOG-RX = PB11 -> R35 100R -> H2.3
 * 依据：数据手册 QFN28 引脚表 —— PB10 = TX1，PB11 = RX1，即 USART1 的
 *       默认 TX/RX 引脚。
 * WCH 库 debug.c 的 USART_Printf_Init() 已按此配置 PB10 为 USART1_TX，
 * 因此 printf 日志直接输出到 H2 排针。 */
#define LOG_TX_PORT         GPIOB
#define LOG_TX_PIN          GPIO_Pin_10
#define LOG_RX_PORT         GPIOB
#define LOG_RX_PIN          GPIO_Pin_11

/* ---- 4.9 USB Power Delivery sink ---- */
#define USB_PD_CC_PORT      GPIOC
#define USB_PD_CC1_PIN      GPIO_Pin_14
#define USB_PD_CC2_PIN      GPIO_Pin_15

/* ========================================================================== *
 *  5. 硬件标度换算（用于把 ADC 码值换算成物理量）
 *  ---------------------------------------------------------------------------
 *  依据：doc/schematic/NETLIST.md 与 SCHEMATIC_DESIGN.md §2.5 / §2.3
 * ========================================================================== */

/** @brief ADC 满量程码值（12 位） */
#define BOARD_ADC_FULL_SCALE        (4095.0f)
/** @brief ADC 参考电压（VDD = 3V3） */
#define BOARD_ADC_VREF              (3.3f)

/** @brief ADC 每 LSB 对应电压（V） */
#define BOARD_ADC_LSB_VOLT          (BOARD_ADC_VREF / BOARD_ADC_FULL_SCALE)

/** @brief VOUT 分压比：(470k + 68k) / 68k ≈ 7.9118 */
#define BOARD_VOUT_DIV_RATIO        ((470.0f + 68.0f) / 68.0f)
/** @brief USB-VBUS 分压比：(470k + 68k) / 68k ≈ 7.9118 */
#define BOARD_VBUS_DIV_RATIO        ((470.0f + 68.0f) / 68.0f)

/** @brief 电流采样增益：INA180A2 = 50 V/V */
#define BOARD_IBUS_GAIN             (50.0f)
/** @brief 电流采样电阻：R7 = 10 mΩ */
#define BOARD_IBUS_SHUNT_OHM        (0.010f)

/**
 * @brief 电流换算系数：ADC 码值 × 本系数 = 安培
 * @note  I(A) = V_adc / (Gain × Rshunt)
 *            = Code × (3.3/4095) / (50 × 0.01)
 *            = Code × 0.0016110...
 */
#define BOARD_IBUS_CODE_TO_A        (BOARD_ADC_LSB_VOLT / (BOARD_IBUS_GAIN * BOARD_IBUS_SHUNT_OHM))

/** @brief VBUS 换算：Code × LSB × 分压比 */
#define BOARD_VBUS_CODE_TO_V        (BOARD_ADC_LSB_VOLT * BOARD_VBUS_DIV_RATIO)
/** @brief VOUT 换算：Code × LSB × 分压比 */
#define BOARD_VOUT_CODE_TO_V        (BOARD_ADC_LSB_VOLT * BOARD_VOUT_DIV_RATIO)


#endif /* __USER_CONFIG_H__ */
