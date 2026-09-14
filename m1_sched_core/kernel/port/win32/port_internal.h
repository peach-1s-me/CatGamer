#ifndef PORT_INTERNAL_H
#define PORT_INTERNAL_H

/* ============================================================
 * Windows 移植 · 内部声明（不属于任何公共接口）
 *
 * 供 kernel/port/win32/ 内部的多个 .c 文件之间共享。
 * ============================================================ */

/* panic 时使用的进程退出码。与测试失败码(1)区分，便于人工/CI 分辨
 * "是断言炸了"还是"测试失败"。 */
#define CATOS_CFG_PANIC_EXIT_CODE   3

/* 初始化运行库后端（日志锁、输出句柄）。由 catos_port_init 调用。 */
void port_rt_init(void);

#endif /* PORT_INTERNAL_H */
