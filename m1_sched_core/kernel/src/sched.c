/* ============================================================
 * 通用调度核心（FR-SCHED-003）
 *
 * 本文件不关心具体调度策略：就绪操作与"选下一个任务"都路由到
 * 当前策略（core->ops），策略本身是可插拔的（见 catos_sched.h）。
 *
 * 两条切换路径：
 *   - k_sched_from_task()：任务上下文，主动让出 CPU（自愿切换）；
 *   - k_sched_from_tick() ：tick 上下文，由 tick 线程抢占（抢占切换）。
 * 两条路径都由移植层提供对应的切换原语（见 catos_port.h）。
 * ============================================================ */

#include "catos_internal.h"

/* 空闲任务 TCB（kernel.c 初始化时分配） */
catos_task_t *k_idle = NULL;

/* 就绪操作路由到当前策略 */
void k_ready_add(catos_core_t *core, catos_task_t *t)
{
    core->ops->ready_add(core, t);
}

void k_ready_remove(catos_core_t *core, catos_task_t *t)
{
    core->ops->ready_remove(core, t);
}

/* 选择下一个要运行的任务；策略无就绪任务时兜底为空闲任务 */
static catos_task_t *k_pick_next(catos_core_t *core)
{
    catos_task_t *next = core->ops->pick_next(core);
    return next != NULL ? next : k_idle;
}

/* 任务上下文切换：当前任务让出 CPU。
 * 注意：被切走的旧任务若非 RUNNING（如 suspend/exit 已改其状态），
 * 保持调用者设定的状态不变；否则恢复为 READY。 */
void k_sched_from_task(void)
{
    catos_core_t *core = core_self();
    catos_task_t *next = k_pick_next(core);

    if (next != core->current) {
        catos_task_t *old = core->current;

        core->current = next;
        next->run_count++;
        if (old != NULL && old->state == CATOS_TASK_RUNNING)
            old->state = CATOS_TASK_READY;   /* 被切走：回到就绪 */
        next->state = CATOS_TASK_RUNNING;    /* 新当前任务：进入运行 */
        catos_port_yield_to(next);           /* 挂起自身；返回时本任务重新获得 CPU */
    }
}

/* tick 上下文切换：由 tick 线程抢占。
 * 先让策略处理 tick（如周期调度释放任务），再选择并切换。 */
void k_sched_from_tick(void)
{
    catos_core_t *core = core_self();

    if (core->ops->on_tick != NULL)
        core->ops->on_tick(core);

    {
        catos_task_t *next = k_pick_next(core);

        if (next != core->current) {
            catos_task_t *old = core->current;

            core->current = next;
            next->run_count++;
            if (old != NULL && old->state == CATOS_TASK_RUNNING)
                old->state = CATOS_TASK_READY;
            next->state = CATOS_TASK_RUNNING;
            catos_port_preempt_to(old, next);
        }
    }
}

/* 选择调度策略（FR-SCHED-003）。
 * 须在 catos_kernel_init 之后、创建任何用户任务之前调用。
 * 缺省策略为固定优先级（catos_fp_sched_ops）。 */
catos_err_t catos_sched_select(const catos_sched_ops_t *ops)
{
    catos_core_t *core = core_self();

    if (ops == NULL || ops->init == NULL || ops->ready_add == NULL ||
        ops->pick_next == NULL)
        return CATOS_E_INVAL;

    catos_port_critical_enter();
    core->ops = ops;
    ops->init(core);   /* 策略初始化自己的私有状态 */
    catos_port_critical_exit();
    return CATOS_OK;
}

/* 空闲任务入口：只做空闲计数，靠 tick 抢占让出 CPU。
 * 需要退出整个程序时由应用层自行处理（见 demo/测试）。 */
void k_idle_task(void *arg)
{
    catos_core_t *core = (catos_core_t *)arg;

    for (;;)
        core->idle_count++;
}
