#include "bsp_gpio.h"

static uint8_t i = 0;


static void ButtonInit(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    GPIO_InitStructure.GPIO_Pin = KEY1_PIN | KEY2_PIN | KEY3_PIN;
    /* 按键低电平有效：板上已有 10k 上拉 + 10nF，内部再开上拉提高抗扰度 */
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(KEY1_PORT, &GPIO_InitStructure);
}




void BspGpioInit(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
    GPIO_InitStructure.GPIO_Pin = LED_RUN_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(LED_RUN_PORT, &GPIO_InitStructure);


    GPIO_InitStructure.GPIO_Pin = VOUT_EN_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(VOUT_EN_PORT, &GPIO_InitStructure);

    ButtonInit();

}

void BspGpioSetVout(uint8_t val)
{
    GPIO_WriteBit(VOUT_EN_PORT, VOUT_EN_PIN, val ? Bit_SET : Bit_RESET);
}

void BspGpioSetLed(uint8_t val)
{
    GPIO_WriteBit(LED_RUN_PORT, LED_RUN_PIN, val ? Bit_SET : Bit_RESET);
}

void BspLedToggle(void)
{
    GPIO_WriteBit(LED_RUN_PORT, LED_RUN_PIN, (i == 0) ? (i = Bit_SET) : (i = Bit_RESET));
}

/**
 * @brief  读全部按键，返回按下按键的掩码。
 * @return 掩码，bit 与 E_BSP_KEY_x 对应：1=该键按下，0=该键未按下；返回 0 表示没有按键按下。
 * @note   本板按键低电平有效，极性转换在本函数内完成，调用方拿到的就是"1=按下"的逻辑值。
 */
uint16_t BspGpioGetButtonMask(void)
{
    uint16_t mask = 0u;

    if (GPIO_ReadInputDataBit(KEY1_PORT, KEY1_PIN) == 0u)
    {
        mask |= (uint16_t)E_BSP_KEY_1;
    }
    if (GPIO_ReadInputDataBit(KEY2_PORT, KEY2_PIN) == 0u)
    {
        mask |= (uint16_t)E_BSP_KEY_2;
    }
    if (GPIO_ReadInputDataBit(KEY3_PORT, KEY3_PIN) == 0u)
    {
        mask |= (uint16_t)E_BSP_KEY_3;
    }

    return mask;
}