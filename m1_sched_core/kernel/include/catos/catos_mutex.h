#ifndef CATOS_MUTEX_H
#define CATOS_MUTEX_H

#include "catos_config.h"
#include "catos_types.h"
#include "catos_list.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * 互斥量（FR-SYNC-001，含优先级继承 FR-PRIO-001）
 *
 * 互斥量实现优先级继承协议（PIP）：
 *   当高优先级任务阻塞在某互斥量上时，持有者临时继承其优先级，
 *   直到释放；沿阻塞链逐级传递（FR-PRIO-002）。因此不会发生无界
 *   优先级反转（FR-PRIO-004）。
 *
 * 支持嵌套获取（递归锁）。M1 暂不支持超时等待（M2 引入）。
 * ============================================================ */

struct catos_task;

typedef struct catos_mutex {
    char               name[CATOS_CFG_TASK_NAME_LEN]; /* 名字（调试用） */
    struct catos_task *owner;      /* 当前持有者；NULL = 空闲 */
    uint32_t           nest_count; /* 嵌套获取计数 */
    catos_list_t       waiters;    /* 等待者队列，按有效优先级升序（高优先级在前） */
    catos_list_t       owner_link; /* 挂到持有者 TCB 的 held_mutexes 链上 */
    uint32_t           wait_prio;  /* 队首等待者的有效优先级；无等待者 = CATOS_CFG_NUM_PRIO */
} catos_mutex_t;

/* 初始化互斥量 */
catos_err_t catos_mutex_init(catos_mutex_t *mutex, const char *name);

/* 获取互斥量：空闲则立即获取；已被其他任务持有则阻塞直到获得（可被优先级继承唤醒）。 */
catos_err_t catos_mutex_lock(catos_mutex_t *mutex);

/* 非阻塞尝试获取：空闲则获取并返回 CATOS_OK；否则返回 CATOS_E_BUSY。 */
catos_err_t catos_mutex_trylock(catos_mutex_t *mutex);

/* 释放互斥量：仅持有者可调用；嵌套获取须逐层释放。
 * 释放时所有权直接转移给最高优先级等待者。 */
catos_err_t catos_mutex_unlock(catos_mutex_t *mutex);

#ifdef __cplusplus
}
#endif

#endif /* CATOS_MUTEX_H */
