#include "bsp_gpio.h"

static uint8_t i = 0;


static void ButtonInit(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    GPIO_InitStructure.GPIO_Pin = KEY1_PIN | KEY2_PIN | KEY3_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
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

uint8_t BspGpioGetButtonLevel(eBspButtonIdDef button_id)
{
    switch (button_id) {
        case E_BSP_KEY_1:
            return GPIO_ReadInputDataBit(KEY1_PORT, KEY1_PIN);
        case E_BSP_KEY_2:
            return GPIO_ReadInputDataBit(KEY2_PORT, KEY2_PIN);
        case E_BSP_KEY_3:
            return GPIO_ReadInputDataBit(KEY3_PORT, KEY3_PIN);
        default:
            return 1; // Default to high level (not pressed)
    }
}