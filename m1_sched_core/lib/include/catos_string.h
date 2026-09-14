#ifndef CATOS_STRING_H
#define CATOS_STRING_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * CatOS 运行库 · 字符串与内存（对应 ISO <string.h>，FR-LIB-003）
 *
 * 本实现完全不依赖宿主库，行为在各平台上一致。
 *
 * 除下列 catos_* 函数外，本模块还导出 ISO 同名符号
 * （memcpy / memset / memmove / memcmp / strlen）：编译器会隐式生成对这些
 * 符号的调用（结构体赋值、大块清零等），必须由 CatOS 自己提供，不得在链接期
 * 落到宿主 libc。这些符号由编译器发起调用，因此不在本头文件中声明。
 *
 * 待扩充（将来按同样形式加入）：
 *   strcat / strncat / strstr / strrchr / strspn / strcspn / memchr ...
 * ============================================================ */

/* 复制 n 字节。dst 与 src 不得重叠（重叠请用 catos_memmove）。返回 dst。 */
void  *catos_memcpy (void *dst, const void *src, size_t n);

/* 把 dst 起的 n 字节置为 (unsigned char)c。返回 dst。 */
void  *catos_memset (void *dst, int c, size_t n);

/* 复制 n 字节，允许 dst 与 src 重叠。返回 dst。 */
void  *catos_memmove(void *dst, const void *src, size_t n);

/* 比较 n 字节：<0 / 0 / >0。 */
int    catos_memcmp (const void *a, const void *b, size_t n);

/* s 的长度（不含结尾 '\0'）。 */
size_t catos_strlen (const char *s);

/* 字符串比较：<0 / 0 / >0。 */
int    catos_strcmp (const char *a, const char *b);

/* 最多比较 n 个字符的字符串比较。 */
int    catos_strncmp(const char *a, const char *b, size_t n);

/* 复制字符串（含结尾 '\0'）。返回 dst。 */
char  *catos_strcpy (char *dst, const char *src);

/* 最多复制 n 个字符；src 短于 n 时用 '\0' 补齐。返回 dst。 */
char  *catos_strncpy(char *dst, const char *src, size_t n);

/* 查找字符 c（含 '\0'）：找到返回指针，否则返回 NULL。 */
char  *catos_strchr (const char *s, int c);

#ifdef __cplusplus
}
#endif

#endif /* CATOS_STRING_H */
