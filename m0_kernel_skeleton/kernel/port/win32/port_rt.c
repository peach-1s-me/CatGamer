/* ============================================================
 * CatOS 运行库后端 · Windows 移植（FR-LIB-006、FR-LIB-009）
 *
 * 本文件与 port.c 一样，属于**唯一允许使用宿主库/API 的一层**。
 * 它实现两类平台相关设施：
 *   1. lib/include/catos_backend.h 的三个钩子（输出 / panic / 退出）；
 *   2. catos_atomic.h 的原子操作（Windows 用 Interlocked* 系列）。
 *
 * 日志后端约定：
 *   - **不做任何换行翻译**：写进去什么字节就出去什么字节。应用自己决定换行；
 *     系统消息的换行由运行库侧的 CATOS_CFG_NL 决定（见 catos_libcfg.h）。
 *   - **单次调用整体写出**：整段缓冲在日志锁内写完，不会被其它任务撕开。
 *   - 用 WriteFile 而非 WriteConsoleA：stdout 被重定向到管道/文件时
 *     WriteConsole 会失败，而 CI 捕获输出、`> 文件` 都依赖重定向。
 * ============================================================ */

#include <windows.h>

#include "catos_backend.h"
#include "catos/catos_atomic.h"

#include "port_internal.h"

static CRITICAL_SECTION g_log_cs;
static HANDLE           g_log_out = INVALID_HANDLE_VALUE;
static int              g_log_ready;

void port_rt_init(void)
{
    /* 初始化失败（资源不足）时保持 g_log_ready = 0：输出被安全丢弃，
     * 而不是对未初始化的临界区加锁（那会是未定义行为）。 */
    g_log_ready = (InitializeCriticalSectionAndSpinCount(&g_log_cs, 4000) != 0);

    g_log_out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (g_log_out == NULL || g_log_out == INVALID_HANDLE_VALUE)
        g_log_out = GetStdHandle(STD_ERROR_HANDLE);
}

/* 注意：port_rt_init 之前调用输出会被安全丢弃（句柄仍为 INVALID_HANDLE_VALUE，
 * 见下面的提前返回），而不是对未初始化的临界区加锁。应用应在 catos_kernel_init()
 * 之后再输出。 */

/* ---------------- 输出 ---------------- */

void catos_port_log_write(const char *buf, size_t len)
{
    DWORD done = 0;

    if (buf == NULL || len == 0)
        return;

    if (!g_log_ready || g_log_out == NULL || g_log_out == INVALID_HANDLE_VALUE)
        return;

    EnterCriticalSection(&g_log_cs);

    while (done < len) {
        DWORD written = 0;

        if (!WriteFile(g_log_out, buf + done, (DWORD)(len - done), &written, NULL))
            break;
        if (written == 0)
            break;                  /* 防御：避免写不进去时死循环 */

        done += written;
    }

    LeaveCriticalSection(&g_log_cs);
}

/* ---------------- 致命错误 ---------------- */

void catos_port_panic(const char *msg)
{
    size_t len     = 0;
    DWORD  written = 0;
    HANDLE err;

    if (msg == NULL)
        msg = "PANIC (no message)\n";

    /* 自己数长度：不调用宿主 strlen，移植层符号表里就只剩 Win32 导入与 CatOS */
    while (msg[len] != '\0')
        len++;

    /* 不加锁：进入 panic 时，日志锁可能正被某个已挂起的任务持有 */
    OutputDebugStringA(msg);

    err = GetStdHandle(STD_ERROR_HANDLE);
    if (err != NULL && err != INVALID_HANDLE_VALUE)
        WriteFile(err, msg, (DWORD)len, &written, NULL);

    /* 同时写到常规输出（stdout）：CI/重定向场景下 stderr 可能不被捕获，
     * 这样 panic 诊断才不会随进程退出一起丢失。不加锁（见上）。 */
    if (g_log_out != NULL && g_log_out != INVALID_HANDLE_VALUE)
        WriteFile(g_log_out, msg, (DWORD)len, &written, NULL);

    if (IsDebuggerPresent())
        DebugBreak();               /* 有调试器：停在故障现场，便于定位 */

    ExitProcess((UINT)CATOS_CFG_PANIC_EXIT_CODE);
}

/* ---------------- 应用退出 ---------------- */

void catos_port_exit(int code)
{
    /* 直接结束进程，不走 CRT 的 exit()：
     *  - 其它任务线程此刻可能处于被挂起状态，CRT 的退出处理（atexit、
     *    DLL detach）在被挂起的线程上执行并不安全；
     *  - 输出无缓冲（日志直接 WriteFile），没有需要刷新的 CRT 缓冲。
     * 退路：若将来出现"挂起线程持有 loader lock 导致挂死"，改用
     * TerminateProcess(GetCurrentProcess(), (UINT)code)。 */
    ExitProcess((UINT)code);
}

/* ---------------- 原子操作（FR-PORT-005） ----------------
 * Win32 的 Interlocked* 系列自带全屏障；LONG 与 int32_t 同为 32 位，
 * 因此下面的指针/类型转换只是类型变换，不改变语义。 */

int32_t catos_atomic_add(catos_atomic_t *p, int32_t v)
{
    LONG old = InterlockedExchangeAdd((volatile LONG *)(void *)p, (LONG)v);

    /* 返回新值（old 是本操作写入前的值）。用无符号相加：即使发生回绕也不触发
     * 有符号溢出的未定义行为。 */
    return (int32_t)((uint32_t)old + (uint32_t)v);
}

int32_t catos_atomic_inc(catos_atomic_t *p)
{
    return (int32_t)InterlockedIncrement((volatile LONG *)(void *)p);
}

int32_t catos_atomic_dec(catos_atomic_t *p)
{
    return (int32_t)InterlockedDecrement((volatile LONG *)(void *)p);
}

int32_t catos_atomic_get(const catos_atomic_t *p)
{
    /* 对齐的 32 位读取本身就是原子的 */
    return (int32_t)*p;
}

void catos_atomic_set(catos_atomic_t *p, int32_t v)
{
    InterlockedExchange((volatile LONG *)(void *)p, (LONG)v);
}

int32_t catos_atomic_swap(catos_atomic_t *p, int32_t v)
{
    return (int32_t)InterlockedExchange((volatile LONG *)(void *)p, (LONG)v);
}

int catos_atomic_cas(catos_atomic_t *p, int32_t expected, int32_t desired)
{
    LONG old = InterlockedCompareExchange((volatile LONG *)(void *)p,
                                          (LONG)desired, (LONG)expected);

    return (old == (LONG)expected) ? 1 : 0;
}
