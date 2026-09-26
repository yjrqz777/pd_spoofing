#ifndef __USER_DISPLAY_H__
#define __USER_DISPLAY_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "user_global.h"

#define DISPLAY_TASK_MS (10)

/* 色相常量：ColorHsvToRgb 的第 1 个参数，取值 0 到 359。
   调色只改这里的 h，饱和度和明度仍在调用处给。 */
#define DISPLAY_HUE_RED     (0u)     /* 红 */
#define DISPLAY_HUE_YELLOW  (60u)    /* 黄 */
#define DISPLAY_HUE_GREEN   (120u)   /* 绿 */
#define DISPLAY_HUE_CYAN    (180u)   /* 青 */
#define DISPLAY_HUE_BLUE    (240u)   /* 蓝 */
#define DISPLAY_HUE_MAGENTA (300u)   /* 品红 */

uint16_t UserDisplayTask(void);
#ifdef __cplusplus
}
#endif

#endif