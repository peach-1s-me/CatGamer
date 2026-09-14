#ifndef CATOS_STDLIB_H
#define CATOS_STDLIB_H

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * CatOS 运行库 · 通用工具（对应 ISO <stdlib.h>，FR-LIB-009⑤）
 *
 * 待扩充：malloc / free / calloc / realloc（将接到 FR-MEM-002 的定长内存池，
 * 见 M2）、strtol / qsort / bsearch ...
 * ============================================================ */

/* 应用结束与结果上报（FR-LIB-009⑤）。语义：
 *  - 宿主目标（Windows/Linux）：作为进程退出码返回给外壳/CI 判定；
 *  - 无进程概念的嵌入式目标：输出结果后停机（或复位）。
 * 本函数不返回。应用的所有退出都应经过它，而不是宿主 exit()。 */
void catos_exit(int code);

/* 致命错误：经断言/panic 通道输出诊断后停机（FR-LIB-005）。不返回。 */
void catos_abort(void);

/* 取绝对值。 */
int  catos_abs(int v);

/* 解析十进制整数（跳过前导空白，可带 '+'/'-'，遇到非数字停止）。
 * 无法解析时返回 0。最小实现：**不检测溢出**（超出 int 范围时结果未定义）。 */
int  catos_atoi(const char *s);

#ifdef __cplusplus
}
#endif

#endif /* CATOS_STDLIB_H */
