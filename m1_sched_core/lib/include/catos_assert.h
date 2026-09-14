#ifndef CATOS_ASSERT_H
#define CATOS_ASSERT_H

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * CatOS 运行库 · 断言与致命错误（对应 ISO <assert.h>，FR-LIB-005）
 *
 * 不使用宿主 assert/abort：诊断信息由运行库格式化，输出与停机由移植层的
 * catos_port_panic 完成（见 catos_port.h）。panic 通道不加锁，因为出错时
 * 日志锁可能正被挂起的任务持有。
 *
 * 待扩充：ISO 同名 assert 门面（等 FR-LIB-010 的第三方代码适配层需要时再加）。
 * ============================================================ */

/* 输出 "<file>:<line>: <msg>" 诊断，然后经移植层停机（不返回）。
 * msg 支持 catos_snprintf 的格式子集（见 catos_stdio.h）。 */
void catos_panic(const char *file, unsigned line, const char *fmt, ...);

/* 断言：条件不成立时进入 panic 通道。 */
#define CATOS_ASSERT(cond) \
    ((cond) ? (void)0 : catos_panic(__FILE__, __LINE__, "assert failed: %s", #cond))

#ifdef __cplusplus
}
#endif

#endif /* CATOS_ASSERT_H */
