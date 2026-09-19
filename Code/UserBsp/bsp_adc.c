#include "bsp_adc.h"

static uint16_t u16Raw[E_BSP_ADC_CH_MAX] = {0};

void BspAdcInit(void)
{
    ADC_InitTypeDef  ADC_InitStructure = {0};
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    memset(&ADC_InitStructure, 0, sizeof(ADC_InitStructure));
    memset(&GPIO_InitStructure, 0, sizeof(GPIO_InitStructure));

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);

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

    ADC_CLKConfig(ADC1, ADC_CLK_Div6);

    ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;
    ADC_InitStructure.ADC_ScanConvMode = ENABLE;
    ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;
    ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
    ADC_InitStructure.ADC_NbrOfChannel = 1;
    ADC_Init(ADC1, &ADC_InitStructure);



    ADC_InjectedSequencerLengthConfig(ADC1, 3);
    ADC_InjectedChannelConfig(ADC1, ADC_VBUS_CHANNEL, 1, ADC_SampleTime_11Cycles);
    ADC_InjectedChannelConfig(ADC1, ADC_VOUT_CHANNEL, 2, ADC_SampleTime_11Cycles);
    ADC_InjectedChannelConfig(ADC1, ADC_IBUS_CHANNEL, 3, ADC_SampleTime_11Cycles);

    ADC_ExternalTrigInjectedConvConfig(ADC1,ADC_ExternalTrigInjecConv_None); 
    ADC_ExternalTrigInjectedConvCmd(ADC1, DISABLE);   /* JEXTEN=0：注入组不走外部触发 */
    ADC_AutoInjectedConvCmd(ADC1, DISABLE);           /* JAUTO=0：否则被规则组自动带走 */

    // ADC_DiscModeChannelCountConfig(ADC1, 1);
    ADC_InjectedDiscModeCmd(ADC1, DISABLE);

    // ADC_SoftwareStartInjectedConvCmd(ADC1, ENABLE);
    ADC_Cmd(ADC1, ENABLE);
    Delay_Ms(1);
    log_info("ADC1 Init Ok");
}

void BspAdcStartInject(void)
{
    if(SET == ADC_GetSoftwareStartInjectedConvCmdStatus(ADC1))
    {
        // log_debug("SoftwareStartInjected return\r\n");
        return;
    }
    ADC_SoftwareStartInjectedConvCmd(ADC1, ENABLE);
    // log_debug("SoftwareStartInjected\r\n");
}



void BspAdcReadRaw(void)
{
    if(RESET == ADC_GetFlagStatus(ADC1, ADC_FLAG_JEOC)) /* 注入规则组转换结束 */
    {
        // memset(u16Raw,0x00,sizeof(u16Raw));
        return;
    }
    ADC_ClearFlag(ADC1, ADC_FLAG_JEOC);
    // while(!ADC_GetFlagStatus(ADC1, ADC_FLAG_JEOC));
    // ADC_ClearFlag(ADC1, ADC_FLAG_JEOC);
    u16Raw[E_BSP_ADC_VBUS] = ADC1->IDATAR1;
    u16Raw[E_BSP_ADC_VOUT] = ADC1->IDATAR2;
    u16Raw[E_BSP_ADC_IBUS] = ADC1->IDATAR3;
    log_info("%04d,%04d,%04d",u16Raw[E_BSP_ADC_VBUS],u16Raw[E_BSP_ADC_VOUT],u16Raw[E_BSP_ADC_IBUS]);
}

uint16_t BspAdcGetRaw(eBspAdcChannelDef channel)
{
    if(channel >= E_BSP_ADC_CH_MAX)
    {
        return 0;
    }
    return u16Raw[channel];
}