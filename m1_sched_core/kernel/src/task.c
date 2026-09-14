/* ============================================================
 * 任务管理（FR-TASK）
 *
 * 任务生命周期：
 *   创建(READY) -> 调度运行(RUNNING) -> 挂起(SUSPENDED) -> 恢复(READY)
 *                                \-> 函数返回(TERMINATED)
 *
 * TCB 采用静态池分配（FR-MEM-001）：确定性、无碎片。
 * "运行中"任务仍位于就绪队列中（running-in-ready 约定），
 * 简化调度：就绪队列最高优先级即应运行任务。
 * ============================================================ */

#include "catos_internal.h"
#include "catos_string.h"

/* 静态 TCB 池 */
static catos_task_t g_task_pool[CATOS_CFG_MAX_TASKS];
static bool         g_task_pool_used[CATOS_CFG_MAX_TASKS];

/* CPU 核数组（按 CATOS_CFG_NUM_CORES） */
catos_core_t g_cores[CATOS_CFG_NUM_CORES];

/* 调度器是否已启动 */
bool g_sched_started = false;

void ktask_init_pool(void)
{
    catos_memset(g_task_pool_used, 0, sizeof(g_task_pool_used));
}

/* 从静态池分配一个 TCB 并初始化（不进入就绪队列，不创建底层执行体） */
catos_task_t *ktask_alloc(const char *name, int prio, size_t stack_size,
                          catos_task_fn_t entry, void *arg)
{
    catos_task_t *t = NULL;
    int i;

    for (i = 0; i < CATOS_CFG_MAX_TASKS; i++) {
        if (!g_task_pool_used[i]) {
            g_task_pool_used[i] = true;
            t = &g_task_pool[i];
            break;
        }
    }
    if (t == NULL)
        return NULL;

    catos_memset(t, 0, sizeof(*t));

    /* 有界复制任务名，避免依赖 strcpy */
    for (i = 0; i < CATOS_CFG_TASK_NAME_LEN - 1 && name != NULL && name[i] != '\0'; i++)
        t->name[i] = name[i];
    t->name[i] = '\0';

    t->state      = CATOS_TASK_READY;
    t->base_prio  = prio;
    t->eff_prio   = prio;          /* 初始有效优先级 = 基优先级 */
    t->stack_size = stack_size ? stack_size : CATOS_CFG_DEFAULT_STACK;
    t->entry      = entry;
    t->arg        = arg;
    t->core       = core_self();
    t->blocked_on = NULL;
    catos_list_init(&t->node);
    catos_list_init(&t->held_mutexes);
    return t;
}

void ktask_free(catos_task_t *t)
{
    int i;

    for (i = 0; i < CATOS_CFG_MAX_TASKS; i++) {
        if (&g_task_pool[i] == t) {
            g_task_pool_used[i] = false;
            return;
        }
    }
}

catos_err_t catos_task_create(catos_task_t **out, const char *name,
                              catos_task_fn_t entry, void *arg,
                              int prio, size_t stack_size)
{
    catos_core_t *core = core_self();
    catos_task_t *t;
    catos_err_t err;

    if (entry == NULL || prio < 0 || prio >= CATOS_CFG_NUM_PRIO)
        return CATOS_E_INVAL;

    catos_port_critical_enter();

    t = ktask_alloc(name, prio, stack_size, entry, arg);
    if (t == NULL) {
        catos_port_critical_exit();
        return CATOS_E_NOMEM;
    }

    err = catos_port_task_start(t);   /* 创建底层执行体（挂起状态） */
    if (err != CATOS_OK) {
        ktask_free(t);
        catos_port_critical_exit();
        return err;
    }

    k_ready_add(core, t);
    if (g_sched_started)
        k_sched_from_task();          /* 新任务优先级更高则立即抢占 */

    catos_port_critical_exit();

    if (out != NULL)
        *out = t;
    return CATOS_OK;
}

catos_err_t catos_task_delete(catos_task_t *task)
{
    catos_core_t *core = core_self();

    if (task == NULL || task == core->current)
        return CATOS_E_INVAL;   /* 不得删除自己，请用 catos_task_exit() */

    catos_port_critical_enter();
    if (task->state == CATOS_TASK_READY)
        k_ready_remove(core, task);
    task->state = CATOS_TASK_TERMINATED;
    catos_port_task_destroy(task);
    ktask_free(task);
    catos_port_critical_exit();
    return CATOS_OK;
}

catos_err_t catos_task_suspend(catos_task_t *task)
{
    catos_core_t *core = core_self();

    if (task == NULL)
        return CATOS_E_INVAL;

    catos_port_critical_enter();
    switch (task->state) {
    case CATOS_TASK_READY:
    case CATOS_TASK_RUNNING:
        k_ready_remove(core, task);
        task->state = CATOS_TASK_SUSPENDED;
        break;
    case CATOS_TASK_BLOCKED:
        /* 阻塞中：先从互斥量等待队列移除（解除 PIP 影响）再挂起 */
        kmutex_task_leave(core, task);
        task->state = CATOS_TASK_SUSPENDED;
        break;
    default:
        catos_port_critical_exit();
        return CATOS_E_STATE;   /* TERMINATED 等不允许挂起 */
    }

    if (g_sched_started)
        k_sched_from_task();    /* 若挂起的是当前任务则让出 CPU */
    catos_port_critical_exit();
    return CATOS_OK;
}

catos_err_t catos_task_resume(catos_task_t *task)
{
    catos_core_t *core = core_self();

    if (task == NULL)
        return CATOS_E_INVAL;

    catos_port_critical_enter();
    if (task->state != CATOS_TASK_SUSPENDED) {
        catos_port_critical_exit();
        return CATOS_E_STATE;
    }
    task->state = CATOS_TASK_READY;
    k_ready_add(core, task);

    if (g_sched_started)
        k_sched_from_task();    /* 恢复的任务优先级更高则立即抢占 */
    catos_port_critical_exit();
    return CATOS_OK;
}

catos_task_t *catos_task_self(void)
{
    return core_self()->current;
}

catos_task_state_t catos_task_state(const catos_task_t *task)
{
    return (task != NULL) ? task->state : CATOS_TASK_TERMINATED;
}

void catos_task_exit(void)
{
    catos_core_t *core = core_self();
    catos_task_t *self = core->current;

    catos_port_critical_enter();
    /* 当前任务可能在就绪结构中（RUNNING 或 READY），需移出 */
    if (self != NULL &&
        (self->state == CATOS_TASK_READY || self->state == CATOS_TASK_RUNNING))
        k_ready_remove(core, self);
    if (self != NULL)
        self->state = CATOS_TASK_TERMINATED;
    if (g_sched_started)
        k_sched_from_task();    /* 让出 CPU，调度其他任务 */
    catos_port_critical_exit();
}

int catos_task_eff_prio(const catos_task_t *task)
{
    return (task != NULL) ? task->eff_prio : -1;
}
