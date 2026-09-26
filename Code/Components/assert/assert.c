/**
 * @file    assert.c
 * @brief   断言组件的运行期失败处理：打印失败位置后关中断停机。
 */

#include "assert.h"
#include "Components/log/src/log.h"   /* log_error：把失败位置输出到串口 */
#include "user_global.h"              /* __disable_irq()：由 core_riscv.h 提供 */

void AssertFailed(const char *pcFile, uint32_t u32Line, const char *pcExpr)
{
    log_error("ASSERT FAILED %s:%lu: (%s)", pcFile, (unsigned long)u32Line, pcExpr);

    __disable_irq();        /* 关中断：不让中断和任务带着错误状态继续跑 */

    for (;;)
    {
        /* 停在这里：现场留给调试器；主循环不再喂狗，IWDG 约 2.7s 后复位 */
    }
}
