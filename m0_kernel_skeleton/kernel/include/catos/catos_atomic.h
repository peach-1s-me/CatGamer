#ifndef CATOS_ATOMIC_H
#define CATOS_ATOMIC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * 原子操作（FR-PORT-005、FR-LIB-009②）
 *
 * 实现由**移植层**提供（平台相关，因此不放在运行库 lib/ 内）：
 *   Windows → InterlockedIncrement / InterlockedCompareExchange ...
 *   嵌入式  → LDREX/STREX（Cortex-M）或临界区保护
 *
 * 应用请使用本接口；不要直接使用宿主原子内建（FR-LIB-009②）。
 *
 * 说明：对齐的 32 位读写本身是原子的，因此 catos_atomic_get 可以直接读；
 * 自增/自减/交换/比较交换这类"读-改-写"操作才必须由移植层保证原子性。
 * ============================================================ */

typedef volatile int32_t catos_atomic_t;

/* 加上 v 并返回**新值**（v 可为负）。 */
int32_t catos_atomic_add(catos_atomic_t *p, int32_t v);

/* 自增并返回**新值**。 */
int32_t catos_atomic_inc(catos_atomic_t *p);

/* 自减并返回**新值**。 */
int32_t catos_atomic_dec(catos_atomic_t *p);

/* 读取当前值。 */
int32_t catos_atomic_get(const catos_atomic_t *p);

/* 写入新值。 */
void    catos_atomic_set(catos_atomic_t *p, int32_t v);

/* 写入 v 并返回**旧值**。 */
int32_t catos_atomic_swap(catos_atomic_t *p, int32_t v);

/* 比较交换：当前值 == expected 时写入 desired。
 * 返回 1 表示成功、0 表示未改动（当前值与 expected 不同）。 */
int     catos_atomic_cas(catos_atomic_t *p, int32_t expected, int32_t desired);

#ifdef __cplusplus
}
#endif

#endif /* CATOS_ATOMIC_H */
