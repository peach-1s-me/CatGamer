#ifndef CATOS_TASK_H
#define CATOS_TASK_H

#include "catos_config.h"
#include "catos_types.h"
#include "catos_list.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 任务状态。状态转换规则见 docs/design/kernel.md。 */
typedef enum {
    CATOS_TASK_READY,       /* 就绪：在就绪队列中，可被调度 */
    CATOS_TASK_RUNNING,     /* 运行中：当前任务（仍位于就绪队列中） */
    CATOS_TASK_BLOCKED,     /* 阻塞：等待某事件/资源（M2 起使用） */
    CATOS_TASK_SUSPENDED,   /* 挂起：被显式挂起，直到 resume */
    CATOS_TASK_TERMINATED,  /* 终止：任务函数已返回 */
} catos_task_state_t;

/* 任务入口函数原型 */
typedef void (*catos_task_fn_t)(void *arg);

struct catos_mutex;   /* 前向声明（优先级继承需要） */

/* 任务控制块 TCB。
 * 结构对应用层透明：应用只能通过 catos_task_* API 间接操作任务。 */
typedef struct catos_task {
    char                name[CATOS_CFG_TASK_NAME_LEN]; /* 任务名（调试/日志用） */
    catos_task_state_t  state;                          /* 当前状态 */
    int                 base_prio;                      /* 基优先级（0 = 最高） */
    int                 eff_prio;                       /* 有效优先级（= base 或被优先级继承提升；就绪队列按此排序） */
    catos_task_fn_t     entry;                          /* 任务入口 */
    void               *arg;                            /* 用户参数 */
    void               *stack_top;                      /* 栈顶（嵌入式移植使用） */
    size_t              stack_size;                     /* 请求的栈大小 */
    catos_list_t        node;                           /* 就绪/等待队列链表节点（同一时间只在一个队列中） */
    catos_list_t        held_mutexes;                   /* 本任务持有的互斥量链表（M1 优先级继承） */
    struct catos_mutex *blocked_on;                     /* 本任务正阻塞等待的互斥量（BLOCKED 时有值） */
    void               *port_priv;                      /* 移植层私有数据（如线程句柄） */
    struct catos_core  *core;                           /* 所属 CPU 核 */
    unsigned int        run_count;                      /* 被调度运行次数（统计） */
} catos_task_t;

/* 创建任务。
 * prio 取值 [0, CATOS_CFG_NUM_PRIO-1]，越小优先级越高。
 * 创建后任务进入就绪队列；调度器启动后，若其优先级高于当前任务会立即抢占。
 * stack_size 为 0 时使用默认栈大小。
 * 返回的 *out 保存新任务句柄（仅用于 suspend/resume 等操作）。 */
catos_err_t catos_task_create(catos_task_t **out, const char *name,
                              catos_task_fn_t entry, void *arg,
                              int prio, size_t stack_size);

/* 删除任务，释放其 TCB。
 * 不能删除当前运行的任务；结束自己请使用 catos_task_exit()。 */
catos_err_t catos_task_delete(catos_task_t *task);

/* 挂起任务：任务进入 SUSPENDED，不再参与调度，直到被 resume 恢复。
 * 可以挂起自己（执行后当前任务让出 CPU）。 */
catos_err_t catos_task_suspend(catos_task_t *task);

/* 恢复被挂起的任务（SUSPENDED -> READY）。
 * 若恢复的任务优先级高于当前任务，会立即抢占。 */
catos_err_t catos_task_resume(catos_task_t *task);

/* 返回当前正在运行的任务。 */
catos_task_t *catos_task_self(void);

/* 结束当前任务：进入 TERMINATED 并让出 CPU，永不返回。
 * 任务函数返回时由移植层自动调用。 */
void catos_task_exit(void);

/* 查询任务状态。 */
catos_task_state_t catos_task_state(const catos_task_t *task);

/* 查询任务有效优先级（FR-PRIO-003 观测接口）。
 * 等于基优先级，或当本任务持有被更高优先级任务等待的互斥量时被临时提升。 */
int catos_task_eff_prio(const catos_task_t *task);

#ifdef __cplusplus
}
#endif

#endif /* CATOS_TASK_H */
