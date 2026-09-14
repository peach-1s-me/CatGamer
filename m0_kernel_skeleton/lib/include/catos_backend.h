#ifndef CATOS_BACKEND_H
#define CATOS_BACKEND_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * CatOS 运行库后端接口（由移植层实现，FR-LIB-006）
 *
 * 运行库（lib/）是平台无关的：它只负责格式化与逻辑，凡是"要与外界打交道"的
 * 动作（写出字节、致命停机、结束应用）都交给下面三个钩子，由移植层实现：
 *
 *   Windows   →  WriteFile(stdout) / ExitProcess
 *   Linux     →  write(1) / _exit
 *   单片机    →  UART 发送寄存器 / 停机（或复位）
 *
 * 移植层需要实现两份契约：
 *   - 内核契约：kernel/include/catos/catos_port.h（切换、tick、临界区…）
 *   - 运行库契约：本头文件（输出、panic、退出）
 * ============================================================ */

/* 写出 len 字节。要求：
 *  - 不做任何换行翻译（'\n' 原样写出）；
 *  - 一次调用是一个整体，不得与其它调用交错；
 *  - 可在任务上下文调用，实现内部须自行串行化。 */
void catos_port_log_write(const char *buf, size_t len);

/* 输出诊断信息后停机（或复位）。**不得返回**。
 * 实现须尽量避免加锁：进入 panic 时，日志锁可能正被已挂起的任务持有。 */
void catos_port_panic(const char *msg);

/* 结束应用：宿主目标→以 code 结束进程；嵌入式目标→输出结果后停机/复位。
 * **不得返回**，由 catos_exit（catos_stdlib.h）调用。 */
void catos_port_exit(int code);

#ifdef __cplusplus
}
#endif

#endif /* CATOS_BACKEND_H */
