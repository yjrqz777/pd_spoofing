/**
 * @file    dev_button.c
 * @brief   按键设备层实现：消抖、单击/双击/长按识别与事件派发。
 */

#include "dev_button.h"


/* 按键句柄链表头 */
static Button* head_handle = NULL;
static uint8_t u8ButtonScanDivider = 0u;

/* 前置声明 */
static void button_handler(Button* handle);
static inline uint8_t button_read_level(Button* handle);

/* 注：以下两个函数按设计应由 BSP 层提供实现（bsp_button.c 或同等文件）。 */
void DevButtonDisIRQ(void)
{
	// #error "本函数应由 BSP 层实现（bsp_button.c 或同等文件）。"
	NVIC_DisableIRQ(TIM3_IRQn);
}

void DevButtonEnIRQ(void)
{
	// #error "本函数应由 BSP 层实现（bsp_button.c 或同等文件）。"
	NVIC_EnableIRQ(TIM3_IRQn);
}

/**
 * @brief  把事件登记进待派发掩码，供主循环上下文取走。
 * @param[in,out] ptButton 产生事件的按键句柄。
 * @param[in] eEvent 待登记的事件。
 */
static void BspButtonQueueEvent(Button *ptButton, ButtonEvent eEvent)
{
	if ((ptButton != NULL) && (eEvent < BTN_EVENT_COUNT)) {
		ptButton->u16PendingEvents |= (uint16_t)(1u << (uint8_t)eEvent);
	}
}


/* 通用多按键实现 */
/**
 * @brief  初始化按键句柄。
 * @param[in] handle 按键句柄。
 * @param[in] pin_level 读该按键 GPIO 电平的函数。
 * @param[in] active_level 按下时对应的电平。
 * @param[in] button_id 按键编号。
 */
void button_init(Button* handle, uint8_t(*pin_level)(uint8_t), uint8_t active_level, uint8_t button_id)
{
	if (!handle || !pin_level) return;  /* 参数检查 */

	memset(handle, 0, sizeof(Button));
	handle->event = (uint8_t)BTN_NONE_PRESS;
	handle->hal_button_level = pin_level;
	handle->button_level = !active_level;  /* 初值取有效电平的反值，避免上电误判为按下 */
	handle->active_level = active_level;
	handle->button_id = button_id;
	handle->state = BTN_STATE_IDLE;
}

/**
 * @brief  注册某个事件的回调函数。
 * @param[in] handle 按键句柄。
 * @param[in] event 触发事件类型。
 * @param[in] cb 回调函数。
 */
void button_attach(Button* handle, ButtonEvent event, BtnCallback cb)
{
	if (!handle || event >= BTN_EVENT_COUNT) return;  /* 参数检查 */
	handle->cb[event] = cb;
}

/**
 * @brief  注销某个事件的回调函数。
 * @param[in] handle 按键句柄。
 * @param[in] event 触发事件类型。
 */
void button_detach(Button* handle, ButtonEvent event)
{
	if (!handle || event >= BTN_EVENT_COUNT) return;  /* 参数检查 */
	handle->cb[event] = NULL;
}

/**
 * @brief  取出当前发生的事件。
 * @param[in] handle 按键句柄。
 * @return 当前事件；句柄为空时返回 BTN_NONE_PRESS。
 */
ButtonEvent button_get_event(Button* handle)
{
	if (!handle) return BTN_NONE_PRESS;
	if (handle->u8DispatchActive != 0u) return (ButtonEvent)handle->u8DispatchedEvent;
	return (ButtonEvent)(handle->event);
}

/**
 * @brief  读取连击次数。
 * @param[in] handle 按键句柄。
 * @return 连击次数。
 */
uint8_t button_get_repeat_count(Button* handle)
{
	if (!handle) return 0;
	return handle->repeat;
}

/**
 * @brief  把按键状态复位到空闲。
 * @param[in] handle 按键句柄。
 */
void button_reset(Button* handle)
{
	if (!handle) return;
	handle->state = BTN_STATE_IDLE;
	handle->ticks = 0;
	handle->repeat = 0;
	handle->event = (uint8_t)BTN_NONE_PRESS;
	handle->debounce_cnt = 0;
	handle->u16PendingEvents = 0u;
	handle->u8DispatchActive = 0u;
}

/**
 * @brief  查询按键当前是否按下。
 * @param[in] handle 按键句柄。
 * @return 1 表示按下，0 表示未按下，-1 表示句柄无效。
 */
int button_is_pressed(Button* handle)
{
	if (!handle) return -1;
	return (handle->button_level == handle->active_level) ? 1 : 0;
}

/**
 * @brief  读取按键电平（内联优化）。
 * @param[in] handle 按键句柄。
 * @return 按键电平。
 */
static inline uint8_t button_read_level(Button* handle)
{
	return handle->hal_button_level(handle->button_id);
}

/**
 * @brief  按键状态机核心：消抖 + 事件判定。
 * @param[in] handle 按键句柄。
 */
static void button_handler(Button* handle)
{
	uint8_t read_gpio_level = button_read_level(handle);

	/* 非空闲状态下累加扫描计数，用于单击/长按的时间判定 */
	if (handle->state > BTN_STATE_IDLE) {
		handle->ticks++;
	}

	/*------------按键消抖------------*/
	if (read_gpio_level != handle->button_level) {
		/* 连续读到同一新电平达到消抖次数才认可 */
		if (++(handle->debounce_cnt) >= DEBOUNCE_TICKS) {
			handle->button_level = read_gpio_level;
			handle->debounce_cnt = 0;
		}
	} else {
		/* 电平未变化，清零消抖计数 */
		handle->debounce_cnt = 0;
	}

	/*-----------------状态机-------------------*/
	switch (handle->state) {
	case BTN_STATE_IDLE:
		if (handle->button_level == handle->active_level) {
			/* 检测到按下 */
			handle->event = (uint8_t)BTN_PRESS_DOWN;
			BspButtonQueueEvent(handle, BTN_PRESS_DOWN);
			handle->ticks = 0;
			handle->repeat = 1;
			handle->state = BTN_STATE_PRESS;
		} else {
			handle->event = (uint8_t)BTN_NONE_PRESS;
		}
		break;

	case BTN_STATE_PRESS:
		if (handle->button_level != handle->active_level) {
			/* 按键抬起 */
			handle->event = (uint8_t)BTN_PRESS_UP;
			BspButtonQueueEvent(handle, BTN_PRESS_UP);
			handle->ticks = 0;
			handle->state = BTN_STATE_RELEASE;
		} else if (handle->ticks > LONG_TICKS) {
			/* 达到长按阈值 */
			handle->event = (uint8_t)BTN_LONG_PRESS_START;
			BspButtonQueueEvent(handle, BTN_LONG_PRESS_START);
			handle->state = BTN_STATE_LONG_HOLD;
		}
		break;

	case BTN_STATE_RELEASE:
		if (handle->button_level == handle->active_level) {
			/* 超时前再次按下，记一次连击 */
			handle->event = (uint8_t)BTN_PRESS_DOWN;
			BspButtonQueueEvent(handle, BTN_PRESS_DOWN);
			if (handle->repeat < PRESS_REPEAT_MAX_NUM) {
				handle->repeat++;
			}
			BspButtonQueueEvent(handle, BTN_PRESS_REPEAT);
			handle->ticks = 0;
			handle->state = BTN_STATE_REPEAT;
		} else if (handle->ticks > SHORT_TICKS) {
			/* 多击窗口超时，按连击次数判定单击/双击 */
			if (handle->repeat == 1) {
				handle->event = (uint8_t)BTN_SINGLE_CLICK;
				BspButtonQueueEvent(handle, BTN_SINGLE_CLICK);
			} else if (handle->repeat == 2) {
				handle->event = (uint8_t)BTN_DOUBLE_CLICK;
				BspButtonQueueEvent(handle, BTN_DOUBLE_CLICK);
			}
			handle->state = BTN_STATE_IDLE;
		}
		break;

	case BTN_STATE_REPEAT:
		if (handle->button_level != handle->active_level) {
			/* 按键抬起 */
			handle->event = (uint8_t)BTN_PRESS_UP;
			BspButtonQueueEvent(handle, BTN_PRESS_UP);
			if (handle->ticks < SHORT_TICKS) {
				handle->ticks = 0;
				handle->state = BTN_STATE_RELEASE;  /* 短于多击窗口，继续等待后续点按 */
			} else {
				handle->state = BTN_STATE_IDLE;  /* 本次连击序列结束 */
			}
		} else if (handle->ticks > SHORT_TICKS) {
			/* 按住超过多击窗口，按普通按下处理 */
			handle->state = BTN_STATE_PRESS;
		}
		break;

	case BTN_STATE_LONG_HOLD:
		if (handle->button_level == handle->active_level) {
			/* 持续按住 */
			handle->event = (uint8_t)BTN_LONG_PRESS_HOLD;
			BspButtonQueueEvent(handle, BTN_LONG_PRESS_HOLD);
		} else {
			/* 长按后抬起 */
			handle->event = (uint8_t)BTN_PRESS_UP;
			BspButtonQueueEvent(handle, BTN_PRESS_UP);
			handle->state = BTN_STATE_IDLE;
		}
		break;

	default:
		/* 状态非法，复位到空闲 */
		handle->state = BTN_STATE_IDLE;
		break;
	}
}

/**
 * @brief  把按键句柄加入扫描链表。
 * @param[in] handle 目标按键句柄。
 * @return 0 成功，-1 已存在，-2 参数无效。
 */
int button_start(Button* handle)
{
	if (!handle) return -2;  /* 参数无效 */

	Button* target = head_handle;
	while (target) {
		if (target == handle) return -1;  /* 已存在 */
		target = target->next;
	}

	handle->next = head_handle;
	head_handle = handle;
	return 0;
}

/**
 * @brief  把按键句柄从扫描链表中移除。
 * @param[in] handle 目标按键句柄。
 */
void button_stop(Button* handle)
{
	if (!handle) return;  /* 参数检查 */

	Button** curr;
	for (curr = &head_handle; *curr; ) {
		Button* entry = *curr;
		if (entry == handle) {
			*curr = entry->next;
			entry->next = NULL;  /* 清空 next，避免残留指向 */
			return;
		} else {
			curr = &entry->next;
		}
	}
}

/**
 * @brief  遍历链表推进所有按键的状态机（扫描间隔 5ms）。
 */
void button_ticks(void)
{
	Button* target;
	for (target = head_handle; target; target = target->next) {
		button_handler(target);
	}
}


/**
 * @brief  在主循环上下文派发中断期间登记的回调。
 * @note   请在主循环的按键任务里调用，这样回调中可以安全地打日志、操作应用状态。
 */
void BspButtonProcessEvents(void)
{
	Button *ptTarget;
	uint16_t PendingEvents;
	uint8_t EventIndex;

	for (ptTarget = head_handle; ptTarget != NULL; ptTarget = ptTarget->next) {
		DevButtonDisIRQ();
		PendingEvents = ptTarget->u16PendingEvents;
		ptTarget->u16PendingEvents = 0u;
		DevButtonEnIRQ();

		for (EventIndex = 0u; EventIndex < (uint8_t)BTN_EVENT_COUNT; EventIndex++) {
			if ((PendingEvents & (uint16_t)(1u << EventIndex)) != 0u) {
				ptTarget->u8DispatchedEvent = EventIndex;
				ptTarget->u8DispatchActive = 1u;
				if (ptTarget->cb[EventIndex] != NULL) {
					ptTarget->cb[EventIndex](ptTarget);
				}
				ptTarget->u8DispatchActive = 0u;
			}
		}
	}
}
