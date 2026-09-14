/* ============================================================
 * Windows (Win32/Win64) 移植层 —— 开发/调试主环境（FR-WIN）
 *
 * 与嵌入式目标共享同一份内核核心源码；平台差异全部封装在本文件。
 *
 * 机制说明：
 *   - 每个任务 = 一个 Win32 线程（创建时为挂起态，首次调度时唤醒）；
 *   - 内核锁 = 递归临界区 CRITICAL_SECTION；
 *   - tick   = 一个高优先级线程 + 可等待定时器，每 CATOS_CFG_TICK_MS 毫秒
 *              调用一次 catos_tick()，驱动抢占调度；
 *   - 切换   = ResumeThread / SuspendThread；
 *   - 空闲任务由调用 catos_kernel_start 的主线程扮演。
 *
 * 正确性要点（详见 docs/design/kernel.md）：
 *   - 内核核心在切换前已把 core->current 置为 next；
 *   - 自愿切换（yield_to）：释放锁 -> 唤醒 next -> 挂起自身 -> 重新上锁；
 *   - 抢占切换（preempt_to）：由 tick 线程持锁执行，挂起 old、唤醒 next，
 *     不挂起调用者。这样内核锁绝不会被挂起的任务线程持有。
 *
 * 已知限制（仿真环境）：
 *   任务代码内不得调用可能阻塞的 Win32 API（如 Sleep、阻塞式磁盘 I/O），
 *   否则可能与抢占式挂起相互干扰。此为教学/调试用途的合理约束。
 *   唯一例外是日志输出后端（port_rt.c）：它直接写宿主 stdout，重定向到
 *   管道且读端不消费时会阻塞，详见 docs/design/kernel.md 第 6.5 节。
 *
 * 本文件与 port_rt.c 一起构成本里程碑唯一允许使用宿主库/API 的一层（FR-LIB-006）。
 * ============================================================ */

#include "catos/catos.h"
#include "catos_backend.h"

#include <windows.h>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

#include "port_internal.h"

/* ---------------- 内核锁 ---------------- */

static CRITICAL_SECTION g_kernel_cs;

void catos_port_init(void)
{
    /* 内核锁创建失败（资源不足）则内核无法安全运行：进入 panic 通道停机。
     * panic 自身不加锁，因此在锁不可用时也能用。 */
    if (!InitializeCriticalSectionAndSpinCount(&g_kernel_cs, 4000))
        catos_port_panic("PANIC\r\n  catos_port_init: kernel lock init failed\r\n");

    /* 运行库后端（日志锁、输出句柄），见 port_rt.c */
    port_rt_init();
}

void catos_port_critical_enter(void)
{
    EnterCriticalSection(&g_kernel_cs);
}

void catos_port_critical_exit(void)
{
    LeaveCriticalSection(&g_kernel_cs);
}

/* ---------------- 任务线程 ---------------- */

/* 任务线程主函数：运行任务入口；返回后通知内核终止本任务 */
static DWORD WINAPI win_task_thread_main(void *param)
{
    catos_task_t *t = (catos_task_t *)param;

    t->entry(t->arg);

    /* 任务函数返回：标记终止并让出 CPU（终止任务永不再被调度） */
    catos_task_exit();

    /* 保险：永不返回（正常路径下已被切换挂起） */
    SuspendThread(GetCurrentThread());
    return 0;
}

catos_err_t catos_port_task_start(catos_task_t *task)
{
    HANDLE h = CreateThread(NULL, (SIZE_T)task->stack_size,
                            win_task_thread_main, task,
                            CREATE_SUSPENDED, NULL);
    if (h == NULL)
        return CATOS_E_NOMEM;
    task->port_priv = (void *)h;
    return CATOS_OK;
}

void catos_port_task_destroy(catos_task_t *task)
{
    if (task->port_priv != NULL) {
        CloseHandle((HANDLE)task->port_priv);
        task->port_priv = NULL;
    }
}

void catos_port_bind_idle(catos_task_t *idle)
{
    HANDLE h;

    /* 空闲任务 = 主线程（调用 catos_kernel_start 的线程）。
     * GetCurrentThread() 是伪句柄，需复制为真实句柄以便其它线程 Resume。 */
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
                    GetCurrentProcess(), &h,
                    0, FALSE, DUPLICATE_SAME_ACCESS);
    idle->port_priv = (void *)h;
}

/* ---------------- 任务切换 ---------------- */

void catos_port_yield_to(catos_task_t *next)
{
    /* 调用者（当前任务线程）持有内核锁：
     * 释放锁 -> 唤醒 next -> 挂起自身 -> 重新上锁后返回。 */
    catos_port_critical_exit();
    ResumeThread((HANDLE)next->port_priv);
    SuspendThread(GetCurrentThread());
    catos_port_critical_enter();
}

void catos_port_preempt_to(catos_task_t *old, catos_task_t *next)
{
    /* 由 tick 线程调用（持锁）：挂起 old、唤醒 next，不挂起调用者。
     * old 此刻不可能持有内核锁（锁在 tick 线程手里），可安全挂起。 */
    ResumeThread((HANDLE)next->port_priv);
    SuspendThread((HANDLE)old->port_priv);
}

/* ---------------- tick 源 ---------------- */

static DWORD WINAPI win_tick_thread_main(void *param)
{
    HANDLE timer;
    LARGE_INTEGER due;

    (void)param;

    timer = CreateWaitableTimer(NULL, FALSE, NULL);
    if (timer == NULL)
        return 1;

    due.QuadPart = 0;   /* 首次立即到期 */
    SetWaitableTimer(timer, &due, CATOS_CFG_TICK_MS, NULL, NULL, FALSE);

    for (;;) {
        WaitForSingleObject(timer, INFINITE);
        catos_tick();   /* 进入内核：检查是否需要抢占 */
    }
    return 0;
}

void catos_port_start_scheduler(void)
{
    HANDLE h = CreateThread(NULL, 0, win_tick_thread_main, NULL, 0, NULL);

    if (h != NULL) {
        /* tick 线程用高优先级，减小定时抖动；句柄无需保留 */
        SetThreadPriority(h, THREAD_PRIORITY_HIGHEST);
        CloseHandle(h);
    }
}

/* ---------------- 位运算（就绪队列用） ---------------- */

unsigned catos_port_ctz(uint32_t x)
{
#if defined(_MSC_VER)
    unsigned long idx;
    if (_BitScanForward(&idx, x))
        return (unsigned)idx;
    return 32u;
#else
    return x ? (unsigned)__builtin_ctz(x) : 32u;
#endif
}
