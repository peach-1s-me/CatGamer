#ifndef TEST_COMMON_H
#define TEST_COMMON_H

/* 测试公共辅助：日志缓冲（原子追加）、断言与结果上报。
 * 每个测试 exe 只包含一次。
 *
 * 平台无关（FR-LIB-008）：只用 CatOS 公共接口——
 *   输出   catos_printf        （运行库，后端在移植层）
 *   原子   catos_atomic_*      （移植层实现）
 *   比较   catos_memcmp        （运行库）
 *   退出   catos_exit          （宿主目标映射为进程退出码，供 CI 判定）
 * 任何宿主头（<stdio.h> <windows.h> …）都不允许出现。 */

#include "catos/catos.h"
#include "catos/catos_atomic.h"
#include "catos_string.h"
#include "catos_stdio.h"
#include "catos_stdlib.h"

#define LOG_CAP 64

static catos_atomic_t g_log_len;        /* 已写入的字符数（超过 LOG_CAP 后只计数不写） */
static char           g_log[LOG_CAP];   /* 未以 '\0' 结尾：打印时用 "%.*s" 限定长度 */
static int            g_fail;

static void logc(char c)
{
    int32_t i = catos_atomic_inc(&g_log_len) - 1;

    if (i >= 0 && i < LOG_CAP)
        g_log[i] = c;
}

static void expect(int cond, const char *what)
{
    if (!cond) {
        catos_printf("  [FAIL] %s\n", what);
        g_fail = 1;
    }
}

/* 打印结果并结束测试：等价于原先的 printf + exit(0/1)，但走 CatOS 接口。
 * 注意精度必须自己钳到 LOG_CAP：g_log_len 是"写入计数"，日志缓冲区本身只有
 * LOG_CAP 字节且不以 '\0' 结尾，直接传 g_log_len 会让格式化函数越界读取。 */
static void report_and_exit(void)
{
    int n = (g_log_len < LOG_CAP) ? (int)g_log_len : LOG_CAP;

    catos_printf("  [%s] log = %.*s\n", g_fail ? "FAIL" : "PASS", n, g_log);
    catos_exit(g_fail ? 1 : 0);
}

#endif /* TEST_COMMON_H */
