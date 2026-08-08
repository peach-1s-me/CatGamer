#ifndef CATOS_CONFIG_H
#define CATOS_CONFIG_H

/* ============================================================
 * CatOS 内核配置
 *
 * 所有可配置项集中于此；修改后需重新编译整个内核。
 * 优先级约定：数值越小优先级越高（0 = 最高，与常见 RTOS 一致）。
 * ============================================================ */

/* 优先级总数。决定就绪队列位图宽度，最大 32（uint32_t 位图）。
 * 注意：数值越大优先级越低，空闲任务使用最低优先级 CATOS_CFG_NUM_PRIO-1。 */
#define CATOS_CFG_NUM_PRIO       32

/* 任务数量上限（静态 TCB 池，确定性分配） */
#define CATOS_CFG_MAX_TASKS      16

/* 任务名长度上限（含结尾 '\0'） */
#define CATOS_CFG_TASK_NAME_LEN  16

/* CPU 核数。当前为单核；结构按 per-core 设计（见 catos_internal.h），
 * 为后续 SMP 扩展预留（需求 A-06）。 */
#define CATOS_CFG_NUM_CORES      1

/* 时钟节拍周期（毫秒），由移植层的 tick 源决定 */
#define CATOS_CFG_TICK_MS        1

/* 默认任务栈大小（字节）。
 * Windows 上栈由操作系统分配，此值作为 CreateThread 的请求值（下限约 64KB）；
 * 嵌入式移植将据此实际分配任务栈。 */
#define CATOS_CFG_DEFAULT_STACK  8192

#endif /* CATOS_CONFIG_H */
