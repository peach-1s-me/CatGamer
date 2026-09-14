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

/* 就绪队列：优先级位图 + 每优先级 FIFO 链表。
 * 位 i = 1 表示优先级 i 存在就绪任务（优先级 0 最高）。
 * 取最高优先级 = 位图最高置位，O(1)（见 ready_queue.c）。 */
typedef struct catos_ready_q {
    uint32_t     bitmap;
    catos_list_t queues[CATOS_CFG_NUM_PRIO];
} catos_ready_q_t;

/* 编译期检查：优先级数必须能放进 32 位位图 */
typedef char catos_assert_num_prio[(CATOS_CFG_NUM_PRIO <= 32) ? 1 : -1];

/* CPU 核结构。
 * 按 per-core 设计（需求 A-06，多核扩展预留）：
 *   - 每个核拥有独立的就绪队列与当前任务；
 *   - 未来 SMP 只需实例化多个 core，并在移植层提供核间唤醒。
 * 当前单核，只使用 g_cores[0]。 */
typedef struct catos_core {
    catos_task_t   *current;      /* 当前运行任务（RUNNING，仍在就绪队列中） */
    catos_ready_q_t ready;        /* 本核就绪队列 */
    uint32_t        total_ticks;  /* 节拍计数 */
    uint32_t        idle_count;   /* 空闲任务循环计数 */
} catos_core_t;

extern catos_core_t g_cores[CATOS_CFG_NUM_CORES];

static inline catos_core_t *core_self(void)
{
    return &g_cores[0];   /* 单核；SMP 时按当前 CPU 编号取对应 core */
}

/* 调度器是否已启动（启动后才允许抢占切换） */
extern bool g_sched_started;

/* 空闲任务 TCB（内核初始化时分配，永远在就绪队列中） */
extern catos_task_t *k_idle;

/* ---- 就绪队列（ready_queue.c） ---- */
void catos_ready_q_init(catos_ready_q_t *rq);
void catos_ready_q_add(catos_ready_q_t *rq, catos_task_t *t);
void catos_ready_q_remove(catos_ready_q_t *rq, catos_task_t *t);
catos_task_t *catos_ready_q_highest(catos_ready_q_t *rq);

/* ---- 任务内部（task.c） ---- */
void ktask_init_pool(void);
catos_task_t *ktask_alloc(const char *name, int prio, size_t stack_size,
                          catos_task_fn_t entry, void *arg);
void ktask_free(catos_task_t *t);

/* ---- 调度内部（sched.c） ---- */
void k_sched_from_task(void);   /* 任务上下文：若需要则自愿切换（让出 CPU） */
void k_sched_from_tick(void);   /* tick 上下文：若需要则抢占切换 */
void k_idle_task(void *arg);    /* 空闲任务入口 */

#ifdef __cplusplus
}
#endif

#endif /* CATOS_INTERNAL_H */
