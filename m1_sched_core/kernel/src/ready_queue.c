/* ============================================================
 * 固定优先级策略的就绪队列（FR-SCHED-004）
 *
 * 数据结构：优先级位图 + 每优先级 FIFO 链表。
 *   - 位 i = 1 表示优先级 i 有就绪任务（优先级 0 最高）；
 *   - 取最高优先级 = 位图最低置位位（通过移植层的 ctz 求得），O(1)。
 *
 * 队列按任务的**有效优先级 eff_prio** 排序（M1 起），因此优先级
 * 继承（PIP）自动生效：任务被提升后移入更高优先级子队列。
 * ============================================================ */

#include "catos_internal.h"

void catos_ready_q_init(catos_ready_q_t *rq)
{
    int i;

    rq->bitmap = 0;
    for (i = 0; i < CATOS_CFG_NUM_PRIO; i++)
        catos_list_init(&rq->queues[i]);
}

void catos_ready_q_add(catos_ready_q_t *rq, catos_task_t *t)
{
    int prio = t->eff_prio;

    if (catos_list_empty(&rq->queues[prio]))
        rq->bitmap |= (uint32_t)(1u << prio);
    catos_list_add_tail(&rq->queues[prio], &t->node);
}

void catos_ready_q_remove(catos_ready_q_t *rq, catos_task_t *t)
{
    int prio = t->eff_prio;

    catos_list_remove(&t->node);
    if (catos_list_empty(&rq->queues[prio]))
        rq->bitmap &= ~(uint32_t)(1u << prio);
}

catos_task_t *catos_ready_q_highest(catos_ready_q_t *rq)
{
    unsigned prio;

    if (rq->bitmap == 0)
        return NULL;

    /* 优先级 0 最高，因此最高优先级 = 最低置位位（尾随零个数）。
     * 例如位 1 置位 -> ctz = 1 -> 优先级 1。 */
    prio = catos_port_ctz(rq->bitmap);
    return CATOS_CONTAINER_OF(catos_list_first(&rq->queues[prio]),
                              catos_task_t, node);
}
