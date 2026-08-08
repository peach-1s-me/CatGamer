#ifndef CATOS_SCHED_H
#define CATOS_SCHED_H

#include "catos_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * 调度器与内核入口
 * ============================================================ */

/* 初始化内核：初始化任务池、就绪队列，创建空闲任务。
 * 必须在创建任何用户任务之前调用，且只调用一次。 */
catos_err_t catos_kernel_init(void);

/* 启动调度器，进入多任务模式，此函数不再返回。
 * 调用线程（通常是 main）成为空闲任务（最低优先级），
 * 仅在无其他就绪任务时被调度。 */
void catos_kernel_start(void);

/* 主动让出 CPU：当前任务移到同优先级队列尾部。
 * 调度器将选择当前就绪集中优先级最高的任务；若优先级更高的任务就绪则立即切换。 */
void catos_sched_yield(void);

/* 时钟节拍入口：由移植层的 tick 源周期性调用（每 CATOS_CFG_TICK_MS 毫秒）。
 * 这是抢占调度的驱动点：tick 到来时检查是否有更高优先级任务需要抢占。 */
void catos_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* CATOS_SCHED_H */
