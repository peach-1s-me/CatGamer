#ifndef CATOS_PORT_H
#define CATOS_PORT_H

#include "catos_config.h"
#include "catos_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * 移植层接口契约（FR-PORT-001）
 *
 * 内核核心只调用本头文件声明的函数。所有平台相关代码都位于
 * kernel/port/<target>/ 下；移植到新平台只需实现本接口。
 *
 * 约定：
 *  - 除特别说明外，这些函数假定调用者已持有内核锁
 *    （catos_port_critical_enter/exit 之间）。
 *  - 所有切换/唤醒操作由内核核心在持锁状态下发起，
 *    以保证就绪状态与底层执行体状态一致。
 * ============================================================ */

struct catos_task;

/* 初始化移植层（创建内核锁等）。由 catos_kernel_init 调用。 */
void catos_port_init(void);

/* 把空闲任务绑定到"当前线程"。
 * 例如 Windows：调用 catos_kernel_start 的主线程即空闲任务。 */
void catos_port_bind_idle(struct catos_task *idle);

/* 为任务创建底层执行体（Windows：一个线程），并以"挂起"状态创建：
 * 不立即运行，由调度器首次切换时唤醒。 */
catos_err_t catos_port_task_start(struct catos_task *task);

/* 销毁任务的底层执行体（Windows：关闭线程句柄）。
 * 仅用于已被终止或永久挂起的任务。 */
void catos_port_task_destroy(struct catos_task *task);

/* 进入/退出临界区（内核锁）。
 * 语义：串行化所有内核入口；可嵌套（递归锁）。 */
void catos_port_critical_enter(void);
void catos_port_critical_exit(void);

/* 自愿切换：当前任务让出 CPU，切到 next。
 * 调用者（当前任务线程）持有内核锁；本函数会依次：
 *   释放内核锁 -> 唤醒 next -> 挂起自身 -> 重新获得内核锁后返回。
 * 返回意味着当前任务再次被调度。
 * 注意：内核核心在调用前已将 core->current 置为 next。 */
void catos_port_yield_to(struct catos_task *next);

/* 抢占切换：由 tick 上下文调用（tick 线程持有内核锁）。
 * 挂起 old 任务的执行体、唤醒 next，然后直接返回（不挂起调用者）。
 * 注意：内核核心在调用前已将 core->current 置为 next。 */
void catos_port_preempt_to(struct catos_task *old, struct catos_task *next);

/* 启动调度器底层：创建 tick 源（Windows：tick 线程）。
 * 返回后由内核切入最高优先级任务。 */
void catos_port_start_scheduler(void);

/* 计算 x 的尾随零个数（最低置位位的位置）。
 * x != 0 时返回 0..31；x == 0 返回 32。
 * 就绪队列取最高优先级（数值最小的优先级，即最低置位位）使用。 */
unsigned catos_port_ctz(uint32_t x);

#ifdef __cplusplus
}
#endif

#endif /* CATOS_PORT_H */
