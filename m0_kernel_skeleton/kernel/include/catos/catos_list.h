#ifndef CATOS_LIST_H
#define CATOS_LIST_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * 侵入式双向链表
 *
 * 内核所有队列（就绪队列、等待队列）的基础。
 * 链表节点内嵌在数据结构（如 TCB）中，链表自身不分配任何内存。
 * 本文件为纯内联实现，仅包含在头文件里。
 * ============================================================ */

typedef struct catos_list {
    struct catos_list *next;
    struct catos_list *prev;
} catos_list_t;

/* 初始化为空链表：头节点的 next/prev 指向自身 */
static inline void catos_list_init(catos_list_t *head)
{
    head->next = head;
    head->prev = head;
}

static inline int catos_list_empty(const catos_list_t *head)
{
    return head->next == head;
}

/* 追加到链尾（FIFO 语义） */
static inline void catos_list_add_tail(catos_list_t *head, catos_list_t *node)
{
    catos_list_t *tail = head->prev;
    node->next = head;
    node->prev = tail;
    tail->next = node;
    head->prev = node;
}

/* 移除节点（节点需已在某链表中） */
static inline void catos_list_remove(catos_list_t *node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = NULL;
    node->prev = NULL;
}

/* 查看链首节点（不移除） */
static inline catos_list_t *catos_list_first(const catos_list_t *head)
{
    return head->next;
}

/* 由节点指针反推包含它的结构体指针 */
#define CATOS_CONTAINER_OF(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

/* 顺序遍历链表 */
#define CATOS_LIST_FOREACH(node, head) \
    for ((node) = (head)->next; (node) != (head); (node) = (node)->next)

#ifdef __cplusplus
}
#endif

#endif /* CATOS_LIST_H */
