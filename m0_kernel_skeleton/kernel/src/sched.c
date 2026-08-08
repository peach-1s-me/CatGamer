/* ============================================================
 * 最小调度器（FR-SCHED-001 的基础）
 *
 * M0 实现"就绪集中取最高优先级"的核心规则：
 *   - 就绪队列非空时，运行其中优先级最高、同级最早进入的任务；
 *   - 空闲任务永远在就绪队列中（最低优先级），保证必有可运行任务。
 *
 * 两种切换路径：
 *   - k_sched_from_task()：任务上下文，主动让出 CPU（自愿切换）；
 *   - k_sched_from_tick() ：tick 上下文，由 tick 线程抢占（抢占切换）。
 * 两条路径都由移植层提供对应的切换原语（见 catos_port.h）。
 *
 * 说明：可插拔调度器接口（sched_ops）属 M1（FR-SCHED-003）。
 * ============================================================ */

#include "catos_internal.h"

/* 空闲任务 TCB（kernel.c 初始化时分配） */
catos_task_t *k_idle = NULL;

/* 选择下一个要运行的任务。
 * 就绪队列恒非空（空闲任务常驻），此处仍兜底返回空闲任务。 */
static catos_task_t *k_pick_next(catos_core_t *core)
{
    catos_task_t *next = catos_ready_q_highest(&core->ready);
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
 * 被抢占的旧任务保持就绪（仍在其优先级队列中），新任务进入运行。 */
void k_sched_from_tick(void)
{
    catos_core_t *core = core_self();
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

/* 空闲任务入口：只做空闲计数，靠 tick 抢占让出 CPU。
 * 需要退出整个程序时由应用层自行处理（见 demo/测试）。 */
void k_idle_task(void *arg)
{
    catos_core_t *core = (catos_core_t *)arg;

    for (;;)
        core->idle_count++;
}
