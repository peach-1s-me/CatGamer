/* ============================================================
 * CatOS 运行库单元测试（FR-TEST-001 中的"运行库"部分）
 *
 * 覆盖：格式化输出、字符串/内存、原子操作、字符分类。
 * 重点回归三类容易写错的用法（都曾在本项目里出错）：
 *   1. "%.*s" 的**精度是上界**：缓冲区可能不以 '\0' 结尾，不得越界查找；
 *   2. 宽度/精度由 '*' 给定时，**必须消费对应参数**，否则后续取参错位；
 *   3. 整数的精度表示**最少位数**（"%.5d"），且"精度 0 + 值 0"输出空串。
 *
 * 本测试不使用 test_common.h：它自己就是运行库的测试，只依赖 CatOS 公共接口。
 * 退出码 0 = 全部通过。
 * ============================================================ */

#include "catos/catos.h"
#include "catos/catos_atomic.h"
#include "catos_string.h"
#include "catos_stdio.h"
#include "catos_stdlib.h"
#include "catos_ctype.h"

static int g_fail;

static void eqs(const char *what, const char *got, const char *want)
{
    if (catos_strcmp(got, want) != 0) {
        catos_printf("  [FAIL] %s: got \"%s\" want \"%s\"\n", what, got, want);
        g_fail = 1;
    }
}

static void eqi(const char *what, int got, int want)
{
    if (got != want) {
        catos_printf("  [FAIL] %s: got %d want %d\n", what, got, want);
        g_fail = 1;
    }
}

/* ---- 格式化 ---- */

static void test_format(void)
{
    char b[128];
    int  r;

    /* 基本转换 */
    catos_snprintf(b, sizeof b, "%d|%i|%u|%x|%X", -42, 42, 42u, 0xbeefu, 0xbeefu);
    eqs("整数转换", b, "-42|42|42|beef|BEEF");

    catos_snprintf(b, sizeof b, "%d", (int)-2147483647 - 1);
    eqs("%d INT_MIN", b, "-2147483648");

    catos_snprintf(b, sizeof b, "%c%%", 'Z');
    eqs("%c 与 %%", b, "Z%");

    catos_snprintf(b, sizeof b, "%s", "hello");
    eqs("%s", b, "hello");

    /* 宽度 / 零填充 / 左对齐 / 负宽度（等价左对齐） */
    catos_snprintf(b, sizeof b, "[%5d][%-5d][%05d][%05d][%*d]", 42, 42, 42, -42, -5, 42);
    eqs("宽度与标志", b, "[   42][42   ][00042][-0042][42   ]");

    /* 整数精度 = 最少位数（且指定精度时忽略 '0' 标志） */
    catos_snprintf(b, sizeof b, "[%.5d][%.0d][%05.3d][%.4x]", 42, 0, 7, 0x2au);
    eqs("整数精度", b, "[00042][][  007][002a]");

    /* %s 精度 + '*'：精度是上界，且不得越过上界查找 '\0' */
    {
        char nb[4];                 /* 故意不以 '\0' 结尾 */

        nb[0] = 'a'; nb[1] = 'b'; nb[2] = 'c'; nb[3] = 'd';
        catos_snprintf(b, sizeof b, "[%.3s]", nb);
        eqs("%.Ns 上界", b, "[abc]");

        catos_snprintf(b, sizeof b, "[%.*s][%.*s]", 2, "hello", -1, "hello");
        eqs("%.*s（负精度=未指定）", b, "[he][hello]");
    }

    /* '*' 宽度/精度必须消费参数：后续转换仍要对齐 */
    catos_snprintf(b, sizeof b, "[%*d][%.*s][%s]", 4, 7, 2, "world", "end");
    eqs("'*' 参数不错位", b, "[   7][wo][end]");

    /* %p：8 位十六进制；宽度照常生效 */
    catos_snprintf(b, sizeof b, "[%p][%12p]", (void *)0x1234, (void *)0x1234);
    eqs("%p", b, "[0x00001234][  0x00001234]");

    /* 不支持的转换原样输出（而不是打印垃圾） */
    catos_snprintf(b, sizeof b, "%q|%ld", 1L);
    eqs("不支持的转换原样输出", b, "%q|%ld");

    /* 返回值 = 本该写出的长度（不含 '\0'），并按缓冲区截断 */
    r = catos_snprintf(b, sizeof b, "%s", "abcdefghij");
    eqi("返回值", r, 10);

    catos_memset(b, 'X', sizeof b);
    r = catos_snprintf(b, 8, "%s", "abcdefghij");
    eqi("截断时返回值仍是全长", r, 10);
    eqs("截断内容", b, "abcdefg");

    b[0] = 'X';
    r = catos_snprintf(b, 1, "abc");
    eqi("n=1 返回值", r, 3);
    eqi("n=1 写入 '\\0'", b[0], 0);

    r = catos_snprintf(NULL, 0, "abc");
    eqi("n=0 返回值（不写缓冲）", r, 3);
}

/* ---- 字符串与内存 ---- */

static void test_string(void)
{
    char m[16];

    catos_strcpy(m, "abcdefghij");
    catos_memmove(m + 2, m, 6);
    eqs("memmove 后向重叠", m, "ababcdefij");

    catos_strcpy(m, "abcdefghij");
    catos_memmove(m, m + 2, 6);
    eqs("memmove 前向重叠", m, "cdefghghij");

    catos_memset(m, 0, sizeof m);
    catos_strncpy(m, "ab", 6);
    eqi("strncpy 补零", catos_memcmp(m, "ab\0\0\0\0", 6), 0);

    eqi("strlen", (int)catos_strlen("hello"), 5);
    eqi("strcmp <", catos_strcmp("abc", "abd") < 0, 1);
    eqi("strncmp 前缀相等", catos_strncmp("abcX", "abcY", 3), 0);
    eqi("strchr 命中", catos_strchr("abc", 'b') != NULL, 1);
    eqi("strchr 未命中", catos_strchr("abc", 'z') == NULL, 1);
    eqi("strchr 找 '\\0'", catos_strchr("abc", '\0') != NULL, 1);
    eqi("memcmp 相等", catos_memcmp("ab", "ab", 2), 0);
    eqi("memcmp 不等", catos_memcmp("ab", "ac", 2) < 0, 1);
}

/* ---- 原子操作 ---- */

static void test_atomic(void)
{
    catos_atomic_t a;

    catos_atomic_set(&a, 0);
    eqi("atomic_inc", (int)catos_atomic_inc(&a), 1);
    eqi("atomic_get", (int)catos_atomic_get(&a), 1);
    eqi("atomic_add", (int)catos_atomic_add(&a, 41), 42);
    eqi("atomic_dec", (int)catos_atomic_dec(&a), 41);

    catos_atomic_set(&a, 5);
    eqi("atomic_swap 返回旧值", (int)catos_atomic_swap(&a, 9), 5);
    eqi("atomic_swap 写入新值", (int)catos_atomic_get(&a), 9);

    eqi("cas 失败", catos_atomic_cas(&a, 1, 2), 0);
    eqi("cas 成功", catos_atomic_cas(&a, 9, 7), 1);
    eqi("cas 后的值", (int)catos_atomic_get(&a), 7);
}

/* ---- 字符分类 ---- */

static void test_ctype(void)
{
    eqi("isdigit", catos_isdigit('7') && !catos_isdigit('a'), 1);
    eqi("isalpha", catos_isalpha('a') && !catos_isalpha('7'), 1);
    eqi("isspace", catos_isspace(' ') && catos_isspace('\n'), 1);
    eqi("toupper", catos_toupper('a'), 'A');
    eqi("tolower", catos_tolower('A'), 'a');
    eqi("abs", catos_abs(-5), 5);
    eqi("atoi", catos_atoi("  -42xyz"), -42);
    eqi("atoi 非法", catos_atoi("xyz"), 0);
}

int main(void)
{
    /* 初始化内核：应用输出的后端（移植层日志锁/句柄）在这里就绪。
     * 本测试不创建任务、不启动调度器——只在主线程上跑断言。 */
    if (catos_kernel_init() != CATOS_OK) {
        catos_printf("kernel init failed\n");
        return 1;
    }

    test_format();
    test_string();
    test_atomic();
    test_ctype();

    catos_printf("  [%s] 运行库单元测试\n", g_fail ? "FAIL" : "PASS");
    catos_exit(g_fail ? 1 : 0);

    return 1;   /* 不可达（catos_exit 不返回） */
}
