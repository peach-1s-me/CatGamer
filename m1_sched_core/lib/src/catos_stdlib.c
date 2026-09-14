/* ============================================================
 * CatOS 运行库 · 通用工具实现（FR-LIB-009⑤）
 *
 * catos_exit / catos_abort 是"应用结束"的两条通道：
 *   - 正常结束（含测试结果上报）→ catos_exit(code)
 *   - 致命错误                    → catos_abort() → panic 通道
 * 两者最终都落到移植层，由移植层决定"宿主上怎么结束、嵌入式上怎么停机"。
 * ============================================================ */

#include "catos_stdlib.h"
#include "catos_assert.h"
#include "catos_ctype.h"
#include "catos_backend.h"

void catos_exit(int code)
{
    /* 移植层实现：宿主目标 → 以 code 结束进程；嵌入式目标 → 输出结果后停机。
     * 契约（catos_backend.h）规定 catos_port_exit 不返回；若某个移植违反了该契约，
     * 属移植层缺陷（应用会在本行之后继续运行）。 */
    catos_port_exit(code);
}

void catos_abort(void)
{
    catos_panic("<abort>", 0, "catos_abort() called");
}

int catos_abs(int v)
{
    /* 注：v == INT_MIN 时结果无法用 int 表示（ISO 同为未定义）。这里用无符号
     * 运算避免有符号溢出（UB），该情况下返回值就是 INT_MIN 本身。 */
    return (v < 0) ? (int)(0u - (unsigned)v) : v;
}

int catos_atoi(const char *s)
{
    int sign = 1;
    int val  = 0;

    while (catos_isspace((unsigned char)*s))
        s++;

    if (*s == '-') {
        sign = -1;
        s++;
    } else if (*s == '+') {
        s++;
    }

    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
    }

    return sign * val;
}
