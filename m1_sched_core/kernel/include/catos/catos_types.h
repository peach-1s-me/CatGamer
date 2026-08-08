#ifndef CATOS_TYPES_H
#define CATOS_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 统一错误码。所有内核 API 返回 CATOS_OK 表示成功，其余为错误。 */
typedef enum {
    CATOS_OK = 0,      /* 成功 */
    CATOS_E_INVAL,     /* 参数非法 */
    CATOS_E_NOMEM,     /* 资源不足（TCB/内存） */
    CATOS_E_STATE,     /* 对象当前状态不允许该操作 */
    CATOS_E_BUSY,      /* 资源忙 */
} catos_err_t;

#ifdef __cplusplus
}
#endif

#endif /* CATOS_TYPES_H */
