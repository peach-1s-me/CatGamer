/* ============================================================
 * 内核初始化与启动
 * ============================================================ */

#include "catos_internal.h"

catos_err_t catos_kernel_init(void)
{
    catos_core_t *core = core_self();

    /* 1. 移植层初始化（创建内核锁等） */
    catos_port_init();

    /* 2. 初始化任务池 */
    ktask_init_pool();
    core->current = NULL;
    core->sched = NULL;

    /* 3. 缺省调度策略：固定优先级（可经 catos_sched_select 更换） */
    core->ops = &catos_fp_sched_ops;
    core->ops->init(core);

    /* 4. 空闲任务：无其他就绪任务时的兜底，由 main 线程扮演。
     *    空闲任务不进入策略的就绪结构，由调度核心在无就绪任务时直接选用。 */
    k_idle = ktask_alloc("idle", CATOS_CFG_NUM_PRIO - 1,
                         CATOS_CFG_DEFAULT_STACK, k_idle_task, core);
    if (k_idle == NULL)
        return CATOS_E_NOMEM;
    catos_port_bind_idle(k_idle);

    g_sched_started = false;
    return CATOS_OK;
}

void catos_kernel_start(void)
{
    /* 1. 启动移植层调度器底层（创建 tick 线程等），此调用不切换 */
    catos_port_start_scheduler();

    /* 2. 置调度器为已启动，并切入最高优先级任务。
     *    本线程（main）在此被挂起，之后以空闲任务身份恢复。 */
    catos_port_critical_enter();
    g_sched_started = true;
    k_sched_from_task();
    catos_port_critical_exit();

    /* 3. 当 main 作为空闲任务被调度时，从这里继续运行空闲循环（永不返回） */
    k_idle_task(core_self());
}

void catos_tick(void)
{
    catos_core_t *core = core_self();

    /* 由移植层 tick 线程调用；进入临界区保证与任务上下文互斥 */
    catos_port_critical_enter();
    core->total_ticks++;
    if (g_sched_started)
        k_sched_from_tick();
    catos_port_critical_exit();
}

void catos_sched_yield(void)
{
    catos_core_t *core = core_self();

    catos_port_critical_enter();
    if (core->current != NULL && core->current->state == CATOS_TASK_RUNNING &&
        core->ops->on_yield != NULL) {
        /* yield 语义由策略决定（如固定优先级：同优先级队列尾部轮转） */
        core->ops->on_yield(core, core->current);
        if (g_sched_started)
            k_sched_from_task();
    }
    catos_port_critical_exit();
}
