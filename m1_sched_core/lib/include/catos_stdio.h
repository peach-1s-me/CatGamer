#ifndef CATOS_STDIO_H
#define CATOS_STDIO_H

#include <stddef.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * CatOS 运行库 · 输出（对应 ISO <stdio.h> 的输出子集，FR-LIB-009①）
 *
 * 语义（与 ISO 的差异都是有意为之，便于跨平台一致）：
 *  - 只做输出，无缓冲：每次调用立即写出，因此没有 fflush 的必要。
 *  - 不做换行翻译：格式串里的 '\n' 原样写出（要 CRLF 请自己写 "\r\n"）。
 *    系统自身消息的换行由 CATOS_CFG_NL 控制（见 catos_libcfg.h）。
 *  - 每次调用是一个整体：不会被其它任务/线程的输出交错。
 *  - 格式化实现在运行库（平台无关），实际输出由移植层后端完成
 *    （Windows → stdout / 串口 → UART，见 catos_port.h）。
 *
 * 待扩充：puts / fputs / fputc / fprintf / fwrite（"是否自动追加换行"的语义
 * 待定后再加）；文件类接口属项目范围外（需求第 6 节）。
 * ============================================================ */

/* 写出单个字符。 */
void catos_putchar(char c);

/* 写出 len 字节；buf 无需以 '\0' 结尾（用于打印非 C 字符串的缓冲）。 */
void catos_write(const char *buf, size_t len);

/* 格式化输出。支持：
 *   转换： %c %% %s %d %i %u %x %X %p
 *   标志： '-'（左对齐）、'0'（零填充；对整数指定精度时按 ISO 忽略）
 *   宽度： 数字或 '*'（"%-6s"、"%-*s"；负宽度等价于左对齐）
 *   精度： ".数字" 或 ".*"（"%.3s"、"%.*s"：%s 为最多字符数、整数为最少位数）
 * 不支持的写法（如浮点 %f、长度修饰 %ld/%zu）：整段**原样输出**，便于在输出中
 * 发现错误，而不是打印垃圾数据；宽度/精度超过 CATOS_CFG_MAX_WIDTH 时按该值处理。
 * 单条消息超过 CATOS_CFG_LOG_BUF-1 时截断。 */
void catos_printf(const char *fmt, ...);

/* 格式化到缓冲区。与 ISO snprintf 一致：n>0 时始终以 '\0' 结尾，
 * 返回"本该写入的长度"（不含 '\0'，可能大于 n-1，用于判断是否被截断）。 */
int  catos_vsnprintf(char *buf, size_t n, const char *fmt, va_list ap);
int  catos_snprintf (char *buf, size_t n, const char *fmt, ...);

/* 刷新输出。当前实现无缓冲，故为空操作；保留接口以备将来引入缓冲。 */
void catos_fflush(void);

#ifdef __cplusplus
}
#endif

#endif /* CATOS_STDIO_H */
