/**
 * @file    dev_button.h
 * @brief   按键设备层接口：消抖、单击/双击/长按识别与事件查询。
 *
 * @note    本模块属于 Device 层：只负责把 GPIO 电平变成按键事件，
 *          不涉及引脚细节（BSP），也不决定"事件该做什么"（Application）。
 */

#ifndef __DEV_BUTTON_H__
#define __DEV_BUTTON_H__

#include <stdint.h>
#include <string.h>

#include "user_global.h"

/* 扫描与判定时间参数（可按需要调整） */
#define TICKS_INTERVAL       (5u)                         /* 扫描间隔（ms） */
#define DEBOUNCE_TICKS       (2u)                         /* 消抖采样次数，最多 7 */
#define SHORT_TICKS          (100u / TICKS_INTERVAL)      /* 多击判定窗口（100ms 折算为扫描次数） */
#define LONG_TICKS           (1000u / TICKS_INTERVAL)     /* 长按判定阈值（1000ms 折算为扫描次数） */
#define PRESS_REPEAT_MAX_NUM (2u)                         /* 连击计数上限 */

/* 前置声明 */
typedef struct _Button Button;

/* 按键回调函数类型 */
typedef void (*BtnCallback)(Button* btn_handle);

/* 按键事件类型 */
typedef enum {
	BTN_PRESS_DOWN = 0,     /* 按下 */
	BTN_PRESS_UP,           /* 抬起 */
	BTN_PRESS_REPEAT,       /* 检测到重复按下 */
	BTN_SINGLE_CLICK,       /* 单击完成 */
	BTN_DOUBLE_CLICK,       /* 双击完成 */
	BTN_LONG_PRESS_START,   /* 长按开始 */
	BTN_LONG_PRESS_HOLD,    /* 长按持续中 */
	BTN_EVENT_COUNT,        /* 事件总数（边界标记） */
	BTN_NONE_PRESS          /* 无事件 */
} ButtonEvent;

/* 按键状态机状态 */
typedef enum {
	BTN_STATE_IDLE = 0,     /* 空闲 */
	BTN_STATE_PRESS,        /* 已按下 */
	BTN_STATE_RELEASE,      /* 已抬起，等待多击超时 */
	BTN_STATE_REPEAT,       /* 重复按下 */
	BTN_STATE_LONG_HOLD     /* 长按保持 */
} ButtonState;

/* 按键句柄 */
struct _Button {
	uint16_t ticks;                     /* 扫描计数 */
	uint8_t  repeat : 4;                /* 连击次数（0~15） */
	uint8_t  event : 4;                 /* 当前事件（0~15） */
	uint8_t  state : 3;                 /* 状态机状态（0~7） */
	uint8_t  debounce_cnt : 3;          /* 消抖计数（0~7） */
	uint8_t  active_level : 1;          /* 按下时的有效电平 */
	uint8_t  button_level : 1;          /* 当前（已消抖）电平 */
	uint8_t  button_id;                 /* 按键编号 */
	uint8_t  (*hal_button_level)(uint8_t button_id);  /* 读 GPIO 电平的函数，由 BSP 提供 */
	BtnCallback cb[BTN_EVENT_COUNT];    /* 各事件对应的回调函数 */
	volatile uint16_t u16PendingEvents; /* 中断内累积的待派发事件位掩码 */
	uint8_t u8DispatchedEvent;          /* 正在派发的事件 */
	uint8_t u8DispatchActive;           /* 是否处于回调派发过程中 */
	Button* next;                       /* 链表中的下一个按键 */
};

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* 板级包装接口 */

void BspButtonProcessEvents(void);

/* 通用多按键接口 */
void button_init(Button* handle, uint8_t(*pin_level)(uint8_t), uint8_t active_level, uint8_t button_id);
void button_attach(Button* handle, ButtonEvent event, BtnCallback cb);
void button_detach(Button* handle, ButtonEvent event);
ButtonEvent button_get_event(Button* handle);
int  button_start(Button* handle);
void button_stop(Button* handle);
void button_ticks(void);

/* 辅助接口 */
uint8_t button_get_repeat_count(Button* handle);
void button_reset(Button* handle);
int button_is_pressed(Button* handle);

#ifdef __cplusplus
}
#endif

#endif /* __DEV_BUTTON_H__ */
