#include "bsp_gpio.h"

static uint8_t i = 0;

void BspGpioInit(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    GPIO_InitStructure.GPIO_Pin = LED_RUN_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(LED_RUN_PORT, &GPIO_InitStructure);


    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    GPIO_InitStructure.GPIO_Pin = VOUT_EN_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(VOUT_EN_PORT, &GPIO_InitStructure);


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