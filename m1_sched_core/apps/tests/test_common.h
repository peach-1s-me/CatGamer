#ifndef TEST_COMMON_H
#define TEST_COMMON_H

/* 测试公共辅助：日志缓冲（原子追加）与断言。每个测试 exe 只包含一次。 */

#include <windows.h>
#include <stdio.h>
#include <string.h>

#define LOG_CAP 64

static volatile LONG g_log_len;
static char          g_log[LOG_CAP];
static int           g_fail;

static void logc(char c)
{
    LONG i = InterlockedIncrement(&g_log_len) - 1;
    if (i >= 0 && i < LOG_CAP)
        g_log[i] = c;
}

static void expect(int cond, const char *what)
{
    if (!cond) {
        printf("  [FAIL] %s\n", what);
        g_fail = 1;
    }
}

#endif /* TEST_COMMON_H */
