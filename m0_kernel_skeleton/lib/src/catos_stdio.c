/* ============================================================
 * CatOS 运行库 · 输出与格式化实现（FR-LIB-009①）
 *
 * 分工：
 *   - 格式化（本文件）：平台无关，一份实现保证各平台输出完全一致；
 *   - 输出（catos_port_log_write）：移植层实现，Windows→stdout、串口→UART。
 *
 * 关键约束：
 *   1. 不做换行翻译，不追加换行：应用写什么字节就输出什么字节。
 *   2. 一次 catos_printf 只调用一次 catos_port_log_write，
 *      因此不会被其它任务/线程的输出撕开（移植层后端只需保证单次调用的原子性）。
 *   3. %s 的精度是**上界**：指定了精度就绝不越过它去查找 '\0'。
 *      测试中用它打印未以 '\0' 结尾的定长缓冲，越界读取是未定义行为。
 * ============================================================ */

#include <stdint.h>

#include "catos_stdio.h"
#include "catos_libcfg.h"
#include "catos_string.h"
#include "catos_backend.h"

/* ---------------- 输出缓冲 ---------------- */

/* 目的缓冲。cap 为容量（含结尾 '\0'）；len 为"本该写出"的长度，
 * 即使超出 cap 也继续累加，用于 snprintf 的返回值语义。 */
typedef struct {
    char  *buf;
    size_t cap;
    size_t len;
} fmt_out_t;

static void out_char(fmt_out_t *o, char c)
{
    if (o->cap > 0 && o->len < o->cap - 1)
        o->buf[o->len] = c;
    o->len++;
}

static void out_pad(fmt_out_t *o, char c, int count)
{
    while (count-- > 0)
        out_char(o, c);
}

/* ---------------- 数值 ---------------- */

/* 把 v 按 base 转成数字字符，倒序存入 tmp，返回位数。 */
static int u2tmp(char *tmp, uint32_t v, unsigned base, int upper)
{
    static const char lower[] = "0123456789abcdef";
    static const char upper_digits[] = "0123456789ABCDEF";
    const char *digits = upper ? upper_digits : lower;
    int n = 0;

    if (v == 0) {
        tmp[n++] = '0';
        return n;
    }

    while (v != 0) {
        tmp[n++] = digits[v % base];
        v /= base;
    }

    return n;
}

/* 输出整数，处理符号、宽度、左对齐、零填充与精度（精度 = 最少数字位数）。
 * 注：数值用 32 位——本项目的目标（win32 及后续 Cortex-M4）都是 32 位；
 * %p 因此打印 32 位地址。将来若有 64 位目标，在此处扩展。 */
static void out_number(fmt_out_t *o, uint32_t uv, int neg, unsigned base,
                       int upper, int width, int left, int zero, int prec)
{
    char tmp[32];
    int  n      = u2tmp(tmp, uv, base, upper);
    int  digits = (prec == 0 && uv == 0) ? 0 : n;   /* ISO：精度 0 且值为 0 → 不输出数字 */
    int  zeros  = (prec > digits) ? (prec - digits) : 0;
    int  total  = digits + zeros + (neg ? 1 : 0);
    int  pad    = (width > total) ? (width - total) : 0;
    int  i;

    if (prec >= 0)
        zero = 0;                   /* ISO：对整数指定精度时忽略 '0' 标志 */

    if (left) {
        if (neg)
            out_char(o, '-');
    } else if (pad > 0 && zero) {
        if (neg)
            out_char(o, '-');
        out_pad(o, '0', pad);
    } else {
        out_pad(o, ' ', pad);
        if (neg)
            out_char(o, '-');
    }

    out_pad(o, '0', zeros);

    for (i = digits - 1; i >= 0; i--)
        out_char(o, tmp[i]);

    if (left)
        out_pad(o, ' ', pad);
}

/* ---------------- 格式化主体 ---------------- */

static void fmt_run(fmt_out_t *o, const char *fmt, va_list ap)
{
    const char *p = fmt;

    while (*p != '\0') {
        const char *spec;              /* 指向 '%'，用于"不支持的转换"原样输出 */
        int  left, zero, width, prec;
        char conv;

        if (*p != '%') {
            out_char(o, *p++);
            continue;
        }

        spec = p;
        p++;                           /* 跨过 '%' */

        left = 0;
        zero = 0;
        for (;;) {
            if (*p == '-') {
                left = 1;
                p++;
            } else if (*p == '0') {
                zero = 1;
                p++;
            } else {
                break;
            }
        }

        width = 0;
        if (*p == '*') {
            /* 宽度由参数给出（ISO：负宽度等价于 '-' 标志 + 绝对值）。
             * 必须消费这个参数，否则 va_list 与实参不再对应。 */
            int w = va_arg(ap, int);

            if (w < 0) {
                left = 1;
                w    = (int)(0u - (unsigned)w);   /* 无符号取负：INT_MIN 也不会溢出 */
            }
            width = w;
            p++;
        } else {
            while (*p >= '0' && *p <= '9') {
                width = width * 10 + (*p - '0');
                p++;
            }
        }
        if (width > CATOS_CFG_MAX_WIDTH)
            width = CATOS_CFG_MAX_WIDTH;          /* 防御：避免天文数字宽度导致长循环 */

        prec = -1;                     /* -1 = 未指定精度 */
        if (*p == '.') {
            p++;
            prec = 0;
            if (*p == '*') {
                /* ISO：负的精度视为"未指定" */
                prec = va_arg(ap, int);
                p++;
                if (prec < 0)
                    prec = -1;
            } else {
                while (*p >= '0' && *p <= '9') {
                    prec = prec * 10 + (*p - '0');
                    p++;
                }
                if (prec > CATOS_CFG_MAX_WIDTH)
                    prec = CATOS_CFG_MAX_WIDTH;
            }
        }

        conv = *p;
        if (conv == '\0') {
            /* 格式串以 '%' 结尾：把剩余部分原样输出 */
            for (; *spec != '\0'; spec++)
                out_char(o, *spec);
            break;
        }
        p++;

        switch (conv) {
        case '%':
            out_char(o, '%');
            break;

        case 'c': {
            char c   = (char)va_arg(ap, int);
            int  pad = (width > 1) ? (width - 1) : 0;
            if (!left)
                out_pad(o, ' ', pad);
            out_char(o, c);
            if (left)
                out_pad(o, ' ', pad);
            break;
        }

        case 's': {
            const char *s = va_arg(ap, const char *);
            size_t      n = 0;
            size_t      i;
            int         pad;

            if (s == NULL)
                s = "(null)";

            if (prec >= 0) {
                /* 精度即上界：不得为了找 '\0' 越过 prec 个字符 */
                while (n < (size_t)prec && s[n] != '\0')
                    n++;
            } else {
                n = catos_strlen(s);
            }

            pad = (width > (int)n) ? (width - (int)n) : 0;
            if (!left)
                out_pad(o, ' ', pad);
            for (i = 0; i < n; i++)
                out_char(o, s[i]);
            if (left)
                out_pad(o, ' ', pad);
            break;
        }

        case 'd':
        case 'i': {
            int      v   = va_arg(ap, int);
            int      neg = 0;
            uint32_t uv;

            if (v < 0) {
                neg = 1;
                uv  = (uint32_t)(-(v + 1)) + 1u;   /* 避免 INT_MIN 取负溢出 */
            } else {
                uv = (uint32_t)v;
            }

            out_number(o, uv, neg, 10, 0, width, left, zero, prec);
            break;
        }

        case 'u':
            out_number(o, va_arg(ap, unsigned), 0, 10, 0, width, left, zero, prec);
            break;

        case 'x':
            out_number(o, va_arg(ap, unsigned), 0, 16, 0, width, left, zero, prec);
            break;

        case 'X':
            out_number(o, va_arg(ap, unsigned), 0, 16, 1, width, left, zero, prec);
            break;

        case 'p': {
            /* 32 位地址：固定 8 位十六进制（用精度实现）。
             * 宽度补白要包住 "0x" 前缀整体，因此这里自己处理宽度。 */
            uint32_t v   = (uint32_t)(uintptr_t)va_arg(ap, void *);
            int      pad = (width > 10) ? (width - 10) : 0;   /* "0x" + 8 位 = 10 */

            if (!left)
                out_pad(o, ' ', pad);
            out_char(o, '0');
            out_char(o, 'x');
            out_number(o, v, 0, 16, 0, 0, 0, 1, 8);
            if (left)
                out_pad(o, ' ', pad);
            break;
        }

        default:
            /* 不支持的转换（如 %f/%ld）：原样输出整段，便于在输出中发现错误，
             * 而不是静默地打印出垃圾数据。 */
            for (; spec < p; spec++)
                out_char(o, *spec);
            break;
        }
    }
}

/* ---------------- 对外接口 ---------------- */

void catos_printf(const char *fmt, ...)
{
    char      buf[CATOS_CFG_LOG_BUF];   /* CATOS_CFG_LOG_BUF > 0（见 catos_libcfg.h） */
    fmt_out_t o;
    va_list   ap;
    size_t    n;

    o.buf = buf;
    o.cap = sizeof buf;
    o.len = 0;

    va_start(ap, fmt);
    fmt_run(&o, fmt, ap);
    va_end(ap);

    /* 收尾：保证 '\0' 结尾（snprintf 语义），并按容量截断 */
    buf[(o.len < o.cap) ? o.len : o.cap - 1] = '\0';
    n = (o.len < o.cap - 1) ? o.len : o.cap - 1;

    /* 一次调用一次写出：整条消息不会被其它任务的输出切开 */
    if (n > 0)
        catos_port_log_write(buf, n);
}

int catos_vsnprintf(char *buf, size_t n, const char *fmt, va_list ap)
{
    fmt_out_t o;

    o.buf = buf;
    o.cap = n;
    o.len = 0;

    fmt_run(&o, fmt, ap);

    if (n > 0)
        buf[(o.len < n) ? o.len : n - 1] = '\0';

    return (int)o.len;                 /* 本该写出的长度（可能大于 n-1） */
}

int catos_snprintf(char *buf, size_t n, const char *fmt, ...)
{
    va_list ap;
    int     r;

    va_start(ap, fmt);
    r = catos_vsnprintf(buf, n, fmt, ap);
    va_end(ap);

    return r;
}

void catos_write(const char *buf, size_t len)
{
    if (len > 0)
        catos_port_log_write(buf, len);
}

void catos_putchar(char c)
{
    catos_port_log_write(&c, 1);
}

void catos_fflush(void)
{
    /* 输出无缓冲：每次调用都已写出，故此处无操作。 */
}
