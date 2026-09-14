/* ============================================================
 * CatOS 运行库 · 断言与致命错误实现（FR-LIB-005）
 *
 * 诊断文本由运行库格式化（平台无关），停机由移植层完成。
 * 本文件不取任何锁：panic 可能发生在别的任务正持有日志锁并处于挂起状态时。
 * ============================================================ */

#include "catos_assert.h"
#include "catos_stdio.h"
#include "catos_libcfg.h"
#include "catos_backend.h"

void catos_panic(const char *file, unsigned line, const char *fmt, ...)
{
    char    detail[CATOS_CFG_LOG_BUF];
    char    msg[CATOS_CFG_LOG_BUF];
    va_list ap;

    va_start(ap, fmt);
    catos_vsnprintf(detail, sizeof detail, fmt, ap);
    va_end(ap);

    /* 换行用 CATOS_CFG_NL：系统消息的换行是可配置的（见 catos_libcfg.h） */
    catos_snprintf(msg, sizeof msg,
                   "PANIC " CATOS_CFG_NL "  %s:%u" CATOS_CFG_NL "  %s" CATOS_CFG_NL,
                   file, line, detail);

    catos_port_panic(msg);              /* 契约：不返回 */
}
