/* ============================================================
 * 固定优先级抢占式调度策略（缺省策略，FR-SCHED-001）
 *
 * 私有状态：优先级位图 + 每优先级 FIFO 的就绪队列（catos_ready_q_t）。
 * 就绪队列按**有效优先级 eff_prio** 排序（支持优先级继承 PIP）。
 *
 * 本文件是 sched_ops 的一个实现；新增策略只需提供另一组 sched_ops
 * 并经 catos_sched_select 注册，不改内核核心（FR-SCHED-003）。
 * ============================================================ */

#include "catos_internal.h"

typedef struct {
    catos_ready_q_t ready;
} fp_sched_t;

/* 单核：固定优先级策略的单实例（多核时放入 core->sched 并每核一份） */
static fp_sched_t g_fp_sched;

static void fp_init(catos_core_t *core)
{
    catos_ready_q_init(&g_fp_sched.ready);
    core->sched = &g_fp_sched;
}

static void fp_ready_add(catos_core_t *core, catos_task_t *t)
{
    (void)core;
    catos_ready_q_add(&g_fp_sched.ready, t);
}

static void fp_ready_remove(catos_core_t *core, catos_task_t *t)
{
    (void)core;
    catos_ready_q_remove(&g_fp_sched.ready, t);
}

static catos_task_t *fp_pick_next(catos_core_t *core)
{
    (void)core;
    return catos_ready_q_highest(&g_fp_sched.ready);
}

static void fp_on_yield(catos_core_t *core, catos_task_t *t)
{
    /* 移到同优先级队列尾部，让同优先级任务轮转（round-robin at same prio） */
    fp_ready_remove(core, t);
    fp_ready_add(core, t);
}

static void fp_on_tick(catos_core_t *core)
{
    (void)core;   /* 固定优先级策略无需 tick 处理；周期调度属 M3 */
}

const catos_sched_ops_t catos_fp_sched_ops = {
    "fixed-priority",
    fp_init,
    fp_ready_add,
    fp_ready_remove,
    fp_pick_next,
    fp_on_yield,
    fp_on_tick,
};
