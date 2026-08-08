/* ============================================================
 * 互斥量与优先级继承协议 PIP（FR-SYNC-001、FR-PRIO）
 *
 * 要点：
 *  - 等待队列按有效优先级升序（高优先级在前），所有权在释放时直接
 *    转移给队首等待者；
 *  - 高优先级任务阻塞到某互斥量时，持有者临时继承其优先级
 *    （FR-PRIO-001），并沿"阻塞链"逐级传递（FR-PRIO-002）；
 *  - 核心是 k_recompute_priority()：从"持有互斥量的等待者优先级"
 *    重算任务有效优先级，并把任务在就绪/等待队列中移到正确位置。
 *
 * 明确不支持（M1）：超时等待（M2）、删除仍持有互斥量的任务、死锁检测。
 * ============================================================ */

#include "catos_internal.h"
#include <string.h>

/* ---- 名字复制 ---- */

static void mutex_name_copy(catos_mutex_t *m, const char *name)
{
    int i;

    for (i = 0; i < CATOS_CFG_TASK_NAME_LEN - 1 && name != NULL && name[i] != '\0'; i++)
        m->name[i] = name[i];
    m->name[i] = '\0';
}

catos_err_t catos_mutex_init(catos_mutex_t *m, const char *name)
{
    if (m == NULL)
        return CATOS_E_INVAL;

    memset(m, 0, sizeof(*m));
    mutex_name_copy(m, name);
    m->owner      = NULL;
    m->nest_count = 0;
    m->wait_prio  = CATOS_CFG_NUM_PRIO;   /* 无等待者 = 最低优先级（32，不在有效范围） */
    catos_list_init(&m->waiters);
    catos_list_init(&m->owner_link);
    return CATOS_OK;
}

/* ---- 等待队列：按有效优先级升序插入（高优先级在前） ---- */

void kmutex_wait_add(catos_mutex_t *m, catos_task_t *t)
{
    catos_list_t *pos;

    /* 找到第一个 eff_prio 比 t 大的任务，插到它前面 */
    CATOS_LIST_FOREACH(pos, &m->waiters) {
        catos_task_t *w = CATOS_CONTAINER_OF(pos, catos_task_t, node);
        if (t->eff_prio < w->eff_prio)
            break;
    }
    catos_list_add_tail(pos, &t->node);   /* 插入到 pos 之前 */

    /* 更新 wait_prio = 队首等待者的有效优先级 */
    m->wait_prio = (uint32_t)CATOS_CONTAINER_OF(
                       catos_list_first(&m->waiters), catos_task_t, node)->eff_prio;
}

void kmutex_wait_remove(catos_mutex_t *m, catos_task_t *t)
{
    catos_list_remove(&t->node);

    if (catos_list_empty(&m->waiters))
        m->wait_prio = CATOS_CFG_NUM_PRIO;
    else
        m->wait_prio = (uint32_t)CATOS_CONTAINER_OF(
                           catos_list_first(&m->waiters), catos_task_t, node)->eff_prio;
}

catos_task_t *kmutex_wait_top(catos_mutex_t *m)
{
    if (catos_list_empty(&m->waiters))
        return NULL;
    return CATOS_CONTAINER_OF(catos_list_first(&m->waiters), catos_task_t, node);
}

/* ---- 有效优先级重算（PIP 核心） ---- */

void k_recompute_priority(catos_core_t *core, catos_task_t *t, unsigned depth)
{
    catos_list_t *pos;
    int newp;

    if (t == NULL || depth > (unsigned)CATOS_CFG_MAX_TASKS)
        return;   /* depth 保护：防止死锁链环导致无限递归 */

    /* 新有效优先级 = max{base, 各持有互斥量的最高优先级等待者} */
    newp = t->base_prio;
    CATOS_LIST_FOREACH(pos, &t->held_mutexes) {
        catos_mutex_t *m = CATOS_CONTAINER_OF(pos, catos_mutex_t, owner_link);
        if ((int)m->wait_prio < newp)
            newp = (int)m->wait_prio;
    }
    if (newp == t->eff_prio)
        return;   /* 未变化 */

    if (t->state == CATOS_TASK_READY || t->state == CATOS_TASK_RUNNING) {
        /* 就绪结构中：先用"旧有效优先级"移出（ready_q 按 eff_prio 索引），
         * 更新后再以新优先级插入，避免位图残留。 */
        k_ready_remove(core, t);
        t->eff_prio = newp;
        k_ready_add(core, t);
    } else if (t->state == CATOS_TASK_BLOCKED && t->blocked_on != NULL) {
        /* 阻塞中：更新后按新优先级在等待队列里重排，并沿阻塞链影响其 owner */
        t->eff_prio = newp;
        {
            catos_mutex_t *m = t->blocked_on;

            kmutex_wait_remove(m, t);
            kmutex_wait_add(m, t);
            if (m->owner != NULL)
                k_recompute_priority(core, m->owner, depth + 1);
        }
    } else {
        /* 其他状态（SUSPENDED/TERMINATED）：仅更新值，恢复/重新就绪时生效 */
        t->eff_prio = newp;
    }
}

/* 把阻塞任务从它正等待的互斥量上移除（挂起/删除阻塞任务时用） */
void kmutex_task_leave(catos_core_t *core, catos_task_t *t)
{
    if (t->blocked_on != NULL) {
        catos_mutex_t *m = t->blocked_on;

        kmutex_wait_remove(m, t);
        t->blocked_on = NULL;
        if (m->owner != NULL)
            k_recompute_priority(core, m->owner, 0);
    }
}

/* ---- 锁操作 ---- */

static void mutex_acquire(catos_core_t *core, catos_mutex_t *m)
{
    catos_task_t *self = core->current;

    m->owner      = self;
    m->nest_count = 1;
    catos_list_add_tail(&self->held_mutexes, &m->owner_link);
}

catos_err_t catos_mutex_lock(catos_mutex_t *m)
{
    catos_core_t *core = core_self();
    catos_task_t *self = core->current;

    if (m == NULL)
        return CATOS_E_INVAL;

    catos_port_critical_enter();

    if (m->owner == NULL) {           /* 空闲：立即获得 */
        mutex_acquire(core, m);
        catos_port_critical_exit();
        return CATOS_OK;
    }
    if (m->owner == self) {           /* 嵌套获取（递归锁） */
        m->nest_count++;
        catos_port_critical_exit();
        return CATOS_OK;
    }

    /* 已被其他任务持有：阻塞等待 */
    self->state      = CATOS_TASK_BLOCKED;
    self->blocked_on = m;
    k_ready_remove(core, self);
    kmutex_wait_add(m, self);
    /* 新等待者可能比 owner 优先级高 -> 提升 owner（沿阻塞链传播） */
    k_recompute_priority(core, m->owner, 0);

    k_sched_from_task();              /* 让出 CPU；被唤醒（获得互斥量）后从此返回 */

    catos_port_critical_exit();
    return CATOS_OK;
}

catos_err_t catos_mutex_trylock(catos_mutex_t *m)
{
    catos_core_t *core = core_self();
    catos_task_t *self = core->current;

    if (m == NULL)
        return CATOS_E_INVAL;

    catos_port_critical_enter();
    if (m->owner == NULL) {
        mutex_acquire(core, m);
        catos_port_critical_exit();
        return CATOS_OK;
    }
    if (m->owner == self) {
        m->nest_count++;
        catos_port_critical_exit();
        return CATOS_OK;
    }
    catos_port_critical_exit();
    return CATOS_E_BUSY;
}

catos_err_t catos_mutex_unlock(catos_mutex_t *m)
{
    catos_core_t *core = core_self();
    catos_task_t *self = core->current;
    catos_task_t *waiter;

    if (m == NULL)
        return CATOS_E_INVAL;

    catos_port_critical_enter();
    if (m->owner != self) {
        catos_port_critical_exit();
        return CATOS_E_INVAL;         /* 仅持有者可释放 */
    }

    m->nest_count--;
    if (m->nest_count > 0) {          /* 嵌套：仍持有 */
        catos_port_critical_exit();
        return CATOS_OK;
    }

    /* 最终释放：从持有链移除 */
    catos_list_remove(&m->owner_link);

    waiter = kmutex_wait_top(m);
    if (waiter != NULL) {
        /* 所有权直接转移给最高优先级等待者 */
        kmutex_wait_remove(m, waiter);
        m->owner = waiter;
        m->nest_count = 1;
        waiter->blocked_on = NULL;
        catos_list_add_tail(&waiter->held_mutexes, &m->owner_link);
        waiter->state = CATOS_TASK_READY;
        k_ready_add(core, waiter);
        /* 新 owner 可能因 m 的其它等待者被提升 */
        k_recompute_priority(core, waiter, 0);
    } else {
        m->owner = NULL;
    }

    /* 释放者自身：有效优先级恢复（不再因 m 被提升） */
    k_recompute_priority(core, self, 0);

    if (g_sched_started)
        k_sched_from_task();          /* 唤醒者优先级更高则抢占 */

    catos_port_critical_exit();
    return CATOS_OK;
}
