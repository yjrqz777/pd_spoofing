/**
 * @file    assert.h
 * @brief   断言组件：编译期静态断言 + 运行期断言。
 *
 * @note    本模块属于 Components：只依赖 log 组件输出失败位置，不依赖板级外设。
 *
 *          STATIC_ASSERT() 在编译期拦下"必须编译期成立"的条件，例如表容量、
 *          结构体尺寸、掩码宽度是否够用；
 *          ASSERT() 在运行期条件不成立时打印文件、行号、表达式，然后关中断停机，
 *          停机后 IWDG 不再被喂，约 2.7s 后复位，现场留给调试器。
 *
 *          断言用于"程序不该走到这里"的场合，不是错误处理：可预期的失败
 *          （参数非法、注册表满、通信超时）应当用返回值或日志上报，
 *          不要指望断言替你兜住。
 *          中断上下文不要用 ASSERT()：失败路径要往串口打印，与本工程
 *          "中断路径禁止 log/printf" 的约定冲突。
 *
 *          用法：
 *            STATIC_ASSERT(3u <= DEV_BTN_NUM_MAX, "button rows exceed table size");
 *            ASSERT(pcBuf != NULL);
 */

#ifndef __ASSERT_H__
#define __ASSERT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* 运行期断言总开关：置 0 时 ASSERT() 展开为空，STATIC_ASSERT() 不受影响 */
#ifndef ASSERT_ENABLE
#define ASSERT_ENABLE   (1)
#endif

/* 把 __LINE__ 拼进标识符，保证同一文件多次使用不重名 */
#define ASSERT_CONCAT_(a, b)   a##b
#define ASSERT_CONCAT(a, b)    ASSERT_CONCAT_(a, b)

/**
 * @brief 编译期断言：条件不成立就编译失败，报错指向使用处所在行。
 * @param[in] cond 编译期可求值的条件。
 * @param[in] msg  失败说明；C11 及以上会出现在编译器的报错文本里。
 * @note  展开成一条声明，写在文件作用域或函数体内，不要放进表达式位置。
 *        C11 之前用负长度数组这个全标准通用写法，老编译器同样能拦下来，
 *        只是此时 msg 不起作用（编译器只会报数组长度非法）。
 */
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
#define STATIC_ASSERT(cond, msg)   _Static_assert((cond), msg)
#else
#define STATIC_ASSERT(cond, msg)   typedef char ASSERT_CONCAT(assert_static_, __LINE__)[(cond) ? 1 : -1]
#endif

/**
 * @brief 运行期断言：条件不成立时打印位置并停机。
 * @param[in] expr 需要成立的条件表达式。
 * @note  失败路径：log_error 输出 → 关中断 → 死循环。只做一次串口输出，不阻塞等待。
 */
#if (ASSERT_ENABLE == 1)
#define ASSERT(expr)   ((expr) ? (void)0 : AssertFailed(__FILE__, __LINE__, #expr))
#else
#define ASSERT(expr)   ((void)0)
#endif

/**
 * @brief 断言失败处理：打印文件、行号、表达式后停机。
 * @param[in] pcFile 源文件名（由 ASSERT 填入 __FILE__）。
 * @param[in] u32Line 行号（由 ASSERT 填入 __LINE__）。
 * @param[in] pcExpr 未成立的条件表达式文本（由 ASSERT 填入 #expr）。
 * @note  一般不由业务代码直接调用，用 ASSERT() 展开。
 */
void AssertFailed(const char *pcFile, uint32_t u32Line, const char *pcExpr);

#ifdef __cplusplus
}
#endif

#endif /* __ASSERT_H__ */
