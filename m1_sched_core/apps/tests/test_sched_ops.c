/* ============================================================
 * CatOS M1 调度策略扩展测试（FR-TEST-003 / FR-SCHED-003）
 *
 * 本测试在**应用层**实现一个全新的"轮转（round-robin）"调度策略
 * （单个 FIFO 就绪队列，无优先级概念），仅实现 catos_sched_ops_t
 * 接口并注册，不改动内核核心 —— 证明调度策略可插拔。
 *
 * 三个任务以不同优先级创建（1/2/3），但在轮转策略下优先级被忽略，
 * 按创建顺序轮流运行，期望日志 "ABCABCABC"。
 * （若仍是固定优先级策略，则高优先级任务会先跑完，日志会是 "AAABBBCCC"。）
 *
 * 退出码 0 = 通过。
 * ============================================================ */

#include "catos/catos.h"
#include "test_common.h"

/* ---- 应用层轮转调度策略 ---- */

typedef struct {
    catos_list_t queue;
} rr_state_t;

static rr_state_t g_rr;

static void rr_init(struct catos_core *core)
{
    (void)core;
    catos_list_init(&g_rr.queue);
}

static void rr_ready_add(struct catos_core *core, catos_task_t *t)
{
    (void)core;
    catos_list_add_tail(&g_rr.queue, &t->node);
}

static void rr_ready_remove(struct catos_core *core, catos_task_t *t)
{
    (void)core;
    catos_list_remove(&t->node);
}

static catos_task_t *rr_pick_next(struct catos_core *core)
{
    (void)core;
    if (catos_list_empty(&g_rr.queue))
        return NULL;
    return CATOS_CONTAINER_OF(catos_list_first(&g_rr.queue), catos_task_t, node);
}

static void rr_on_yield(struct catos_core *core, catos_task_t *t)
{
    (void)core;
    (void)t;
    /* 队首移到队尾（把"自己"移到队尾） */
    catos_list_remove(&t->node);
    catos_list_add_tail(&g_rr.queue, &t->node);
}

static void rr_on_tick(struct catos_core *core)
{
    (void)core;   /* 轮转策略无需 tick 处理 */
}

static const catos_sched_ops_t rr_ops = {
    "round-robin (test policy)",
    rr_init,
    rr_ready_add,
    rr_ready_remove,
    rr_pick_next,
    rr_on_yield,
    rr_on_tick,
};

/* ---- 测试任务 ---- */

static catos_atomic_t g_done;   /* 完成的任务数（原子） */

static void task_round(void *arg)
{
    char c = (char)(uintptr_t)arg;
    int k;

    for (k = 0; k < 3; k++) {
        logc(c);
        catos_sched_yield();   /* 让出 CPU：轮转策略下轮到下一个任务 */
    }
    catos_atomic_inc(&g_done);
    catos_task_exit();
}

static void checker(void *arg)
{
    (void)arg;
    while (g_done < 3)
        catos_sched_yield();

    /* 轮转：A B C A B C A B C（按创建顺序，忽略优先级） */
    expect(g_log_len == 9 && catos_memcmp(g_log, "ABCABCABC", 9) == 0,
           "rr: round-robin order 'ABCABCABC' (priority ignored)");

    report_and_exit();
}

int main(void)
{
    catos_task_t *t;

    if (catos_kernel_init() != CATOS_OK) {
        catos_printf("kernel init failed\n");
        return 1;
    }
    /* 切换为轮转策略（须在创建用户任务之前） */
    if (catos_sched_select(&rr_ops) != CATOS_OK) {
        catos_printf("sched_select failed\n");
        return 1;
    }

    /* 以不同优先级创建：轮转策略下优先级被忽略，按创建顺序运行 */
    catos_task_create(&t, "A", task_round, (void *)(uintptr_t)'A', 1, 0);
    catos_task_create(&t, "B", task_round, (void *)(uintptr_t)'B', 2, 0);
    catos_task_create(&t, "C", task_round, (void *)(uintptr_t)'C', 3, 0);
    catos_task_create(&t, "check", checker, NULL, 5, 0);

    catos_kernel_start();   /* 不再返回 */
    return 1;
}
