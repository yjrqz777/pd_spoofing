#include "bsp_adc.h"

static uint16_t u16Raw[E_BSP_ADC_CH_MAX] = {0};

/*
 * ADC1 初始化：注入组 3 路（VBUS=PC0/IN10、VOUT=PA0/IN0、IBUS=PA4/IN4），软件触发。
 * 只输出原始码值，码值到物理量的换算属于 Device 层。
 */
void BspAdcInit(void)
{
    ADC_InitTypeDef  ADC_InitStructure = {0};
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    memset(&ADC_InitStructure, 0, sizeof(ADC_InitStructure));
    memset(&GPIO_InitStructure, 0, sizeof(GPIO_InitStructure));

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);   /* 必须在 ADC_DeInit 之前 */

    GPIO_InitStructure.GPIO_Pin = ADC_VBUS_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(ADC_VBUS_PORT, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = ADC_VOUT_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(ADC_VOUT_PORT, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = ADC_IBUS_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(ADC_IBUS_PORT, &GPIO_InitStructure);

    ADC_DeInit(ADC1);

    ADC_CLKConfig(ADC1, ADC_CLK_Div6);                     /* ADCCLK = 48MHz / 6 = 8MHz */

    ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;
    /*
     * ADC_ScanConvMode：转换模式选择，对规则组和注入组同时生效（本工程未使用规则组）。
     *   DISABLE(0) = 单次单通道模式：每个启动事件只转换序列中的第 1 路。
     *                注入组只转 JSQ(4-JL) 起点的第 1 个通道，数据只写 IDATAR1，
     *                IDATAR2/3 保持初值 0；且硬件会同时置 EOC 和 JEOC。
     *   ENABLE (1) = 单次扫描模式：按 ADC_ISQR 中的顺序对选中的注入通道逐个转换，
     *                3 路依次写入 IDATAR1/2/3，整条序列转完后才置 JEOC。
     * 本工程要“一次 JSWSTART 拿齐 VBUS/VOUT/IBUS 三路”，因此必须 ENABLE。
     */
    ADC_InitStructure.ADC_ScanConvMode = ENABLE;
    ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;
    ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
    ADC_InitStructure.ADC_NbrOfChannel = 1;
    ADC_Init(ADC1, &ADC_InitStructure);

    ADC_InjectedSequencerLengthConfig(ADC1, 3);                                  
    ADC_InjectedChannelConfig(ADC1, ADC_VBUS_CHANNEL, 1, ADC_SampleTime_11Cycles);  /* rank1 -> IDATAR1 */
    ADC_InjectedChannelConfig(ADC1, ADC_VOUT_CHANNEL, 2, ADC_SampleTime_11Cycles);  /* rank2 -> IDATAR2 */
    ADC_InjectedChannelConfig(ADC1, ADC_IBUS_CHANNEL, 3, ADC_SampleTime_11Cycles);  /* rank3 -> IDATAR3 */

    ADC_ExternalTrigInjectedConvConfig(ADC1,ADC_ExternalTrigInjecConv_None);     /* JEXTSEL=111：JSWSTART 软件触发 */
    ADC_ExternalTrigInjectedConvCmd(ADC1, DISABLE);       /* JEXTTRIG=0：不走定时器/EXTI 等外部触发 */
    ADC_AutoInjectedConvCmd(ADC1, DISABLE);               /* IAUTO=0：注入组不被规则组自动带出 */

    ADC_InjectedDiscModeCmd(ADC1, DISABLE);               /* 关闭注入组间断模式，一次触发转完 3 路 */

    ADC_Cmd(ADC1, ENABLE);
    Delay_Ms(1);                                          /* 等待 ADC 上电稳定 */
    log_info("ADC1 Init Ok");
}

void BspAdcStartInject(void)
{
    if(SET == ADC_GetSoftwareStartInjectedConvCmdStatus(ADC1))
    {
        return;
    }
    ADC_SoftwareStartInjectedConvCmd(ADC1, ENABLE);
}



/*
 * 读取最近一次注入转换结果（3 路码值）。
 * @retval E_OK   本帧有效：JEOC 已置位，u16Raw[] 更新为本帧数据
 * @retval E_BUSY 本帧无新数据：JEOC 未置位（未触发或首次调用），u16Raw[] 仍为上一帧
 */
eStatusDef BspAdcReadRaw(void)
{
    if(RESET == ADC_GetFlagStatus(ADC1, ADC_FLAG_JEOC)) /* 注入组转换完成标志 */
    {
        // memset(u16Raw,0x00,sizeof(u16Raw));
        log_warn("ADC JEOC not set, no new data");
        return E_BUSY;
    }
    ADC_ClearFlag(ADC1, ADC_FLAG_JEOC);
    u16Raw[E_BSP_ADC_VBUS] = ADC1->IDATAR1;
    u16Raw[E_BSP_ADC_VOUT] = ADC1->IDATAR2;
    u16Raw[E_BSP_ADC_IBUS] = ADC1->IDATAR3;
    // log_info("%04d,%04d,%04d",u16Raw[E_BSP_ADC_VBUS],u16Raw[E_BSP_ADC_VOUT],u16Raw[E_BSP_ADC_IBUS]);
    return E_OK;
}

uint16_t BspAdcGetRaw(eBspAdcChannelDef channel)
{
    if(channel >= E_BSP_ADC_CH_MAX)
    {
        log_error("ADC channel %d out of range", channel);
        return 0;
    }
    return u16Raw[channel];
}
