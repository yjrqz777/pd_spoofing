#ifndef __BSP_GPIO_H__
#define __BSP_GPIO_H__

#include "user_global.h"


void BspGpioSetLed(uint8_t val);
void BspGpioSetVout(uint8_t val);
void BspLedToggle(void);
void BspGpioInit(void);

#endif 