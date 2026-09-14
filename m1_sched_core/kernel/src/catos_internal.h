#ifndef CATOS_INTERNAL_H
#define CATOS_INTERNAL_H

/* ============================================================
 * 内核核心内部接口
 *
 * 仅供 kernel/src/ 下的源文件使用，绝不对外暴露。
 * 本文件之外的核心/移植边界是 kernel/include/catos/catos_port.h。
 *
 * 头文件白名单（FR-LIB-002）：核心层只允许包含编译器提供的独立环境头
 * （<stdint.h> <stddef.h> <stdbool.h> <limits.h> <float.h> <stdarg.h> <iso646.h>）、
 * CatOS 公共头（"catos/catos.h"）与运行库头（"catos_string.h" 等）。
 * 禁止包含宿主头（<string.h> <stdio.h> <windows.h> …）：字符串/内存操作用
 * 运行库的 catos_mem* / catos_str*；输出用 catos_printf。
 * ============================================================ */

#include "catos/catos.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 固定优先级策略的就绪队列：优先级位图 + 每优先级 FIFO 链表。
 * 位 i = 1 表示优先级 i 存在就绪任务（优先级 0 最高）。
 * 取最高优先级 = 最低置位位（见 ready_queue.c）。 */
typedef struct catos_ready_q {
    uint32_t     bitmap;
    catos_list_t queues[CATOS_CFG_NUM_PRIO];
} catos_ready_q_t;

/* 编译期检查：优先级数必须能放进 32 位位图 */
typedef char catos_assert_num_prio[(CATOS_CFG_NUM_PRIO <= 32) ? 1 : -1];

/* CPU 核结构。
 * 按 per-core 设计（需求 A-06，多核扩展预留）：
 *   - 当前任务、调度策略及其私有状态都在 core 上；
 *   - 未来 SMP 只需实例化多个 core，并在移植层提供核间唤醒。
 * 当前单核，只使用 g_cores[0]。 */
typedef struct catos_core {
    catos_task_t          *current;   /* 当前运行任务（RUNNING，运行中仍在策略的就绪结构中） */
    const catos_sched_ops_t *ops;     /* 当前调度策略（FR-SCHED-003） */
    void                  *sched;     /* 策略私有状态（由策略的 init 设置） */
    uint32_t               total_ticks;
    uint32_t               idle_count;
} catos_core_t;

extern catos_core_t g_cores[CATOS_CFG_NUM_CORES];

static inline catos_core_t *core_self(void)
{
    return &g_cores[0];   /* 单核；SMP 时按当前 CPU 编号取对应 core */
}

/* 调度器是否已启动（启动后才允许抢占切换） */
extern bool g_sched_started;

/* 空闲任务 TCB（内核初始化时分配）。无其他就绪任务时的兜底。 */
extern catos_task_t *k_idle;

/* 固定优先级策略（缺省策略，sched_fp.c） */
extern const catos_sched_ops_t catos_fp_sched_ops;

/* ---- 固定优先级策略的就绪队列操作（ready_queue.c，供 sched_fp.c 与优先级重排使用） ---- */
void catos_ready_q_init(catos_ready_q_t *rq);
void catos_ready_q_add(catos_ready_q_t *rq, catos_task_t *t);
void catos_ready_q_remove(catos_ready_q_t *rq, catos_task_t *t);
catos_task_t *catos_ready_q_highest(catos_ready_q_t *rq);

/* ---- 调度核心（sched.c）：就绪操作路由到当前策略 ---- */
void k_ready_add(catos_core_t *core, catos_task_t *t);
void k_ready_remove(catos_core_t *core, catos_task_t *t);
void k_sched_from_task(void);   /* 任务上下文：若需要则自愿切换（让出 CPU） */
void k_sched_from_tick(void);   /* tick 上下文：若需要则抢占切换 */
void k_idle_task(void *arg);    /* 空闲任务入口 */

/* ---- 任务内部（task.c） ---- */
void ktask_init_pool(void);
catos_task_t *ktask_alloc(const char *name, int prio, size_t stack_size,
                          catos_task_fn_t entry, void *arg);
void ktask_free(catos_task_t *t);

/* ---- 互斥量/优先级继承（mutex.c） ---- */

/* 把任务 t 插入互斥量 m 的等待队列（按有效优先级升序，高优先级在前），并更新 m->wait_prio */
void kmutex_wait_add(catos_mutex_t *m, catos_task_t *t);
/* 从等待队列移除任务 t，并更新 m->wait_prio */
void kmutex_wait_remove(catos_mutex_t *m, catos_task_t *t);
/* 取等待队列队首（最高优先级等待者），无则返回 NULL */
catos_task_t *kmutex_wait_top(catos_mutex_t *m);

/* 把任务 t 从它正阻塞的互斥量等待队列中移除并解除 blocked_on，
 * 并重算该互斥量 owner 的优先级（供挂起/删除阻塞任务时使用）。 */
void kmutex_task_leave(catos_core_t *core, catos_task_t *t);

/* PIP 核心：重算任务 t 的有效优先级 = max{base, t 所持各互斥量的 wait_prio}；
 * 若变化则把它在就绪队列/等待队列中移动到正确位置，并沿阻塞链传播。
 * depth 为递归深度上限，防止死锁链环导致无限递归。 */
void k_recompute_priority(catos_core_t *core, catos_task_t *t, unsigned depth);

#ifdef __cplusplus
}
#endif

#endif /* CATOS_INTERNAL_H */
