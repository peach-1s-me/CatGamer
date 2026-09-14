/* ============================================================
 * CatOS 运行库 · 字符串与内存实现（FR-LIB-003）
 *
 * 实现要点：
 *  1. 实际实现以 ISO 同名符号（memcpy/memset/memmove/memcmp/strlen）导出，
 *     catos_* 为薄封装。这样编译器隐式生成的库调用（结构体赋值、大块清零）
 *     直接命中同一份实现，不会在链接期落到宿主 libc。
 *  2. 本文件必须用 -fno-builtin 编译（见 CMakeLists.txt）：否则编译器可能把
 *     手写循环识别成库调用，生成对函数自身的调用（无限递归）。
 *  3. 只用字节循环，不做字宽/对齐技巧——这些函数同时承担 libc 同名符号的职责，
 *     必须对任意对齐都正确（性能优化留给后续里程碑，且须保持可读）。
 * ============================================================ */

#include "catos_string.h"

void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char       *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    size_t i;

    for (i = 0; i < n; i++)
        d[i] = s[i];

    return dst;
}

void *catos_memcpy(void *dst, const void *src, size_t n)
{
    return memcpy(dst, src, n);
}

void *memset(void *dst, int c, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    size_t i;

    for (i = 0; i < n; i++)
        d[i] = (unsigned char)c;

    return dst;
}

void *catos_memset(void *dst, int c, size_t n)
{
    return memset(dst, c, n);
}

void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char       *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;

    if (d == s || n == 0)
        return dst;

    if (d < s) {
        /* 目标在源之前：从前往后复制（比较的是同一对象/无关对象的地址序，
         * 这里只用于选择方向，不依赖其可移植语义）。 */
        size_t i;
        for (i = 0; i < n; i++)
            d[i] = s[i];
    } else {
        /* 目标在源之后：从后往前复制，避免覆盖尚未读取的源数据。 */
        size_t i = n;
        while (i-- > 0)
            d[i] = s[i];
    }

    return dst;
}

void *catos_memmove(void *dst, const void *src, size_t n)
{
    return memmove(dst, src, n);
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *p = (const unsigned char *)a;
    const unsigned char *q = (const unsigned char *)b;
    size_t i;

    for (i = 0; i < n; i++) {
        if (p[i] != q[i])
            return (int)p[i] - (int)q[i];
    }

    return 0;
}

int catos_memcmp(const void *a, const void *b, size_t n)
{
    return memcmp(a, b, n);
}

size_t strlen(const char *s)
{
    const char *p = s;

    while (*p != '\0')
        p++;

    return (size_t)(p - s);
}

size_t catos_strlen(const char *s)
{
    return strlen(s);
}

int catos_strcmp(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }

    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int catos_strncmp(const char *a, const char *b, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        if (a[i] != b[i])
            return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
        if (a[i] == '\0')
            break;
    }

    return 0;
}

char *catos_strcpy(char *dst, const char *src)
{
    char *d = dst;

    while ((*d++ = *src++) != '\0')
        ;

    return dst;
}

char *catos_strncpy(char *dst, const char *src, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        dst[i] = src[i];
        if (src[i] == '\0') {
            /* src 短于 n：其余位置补 '\0'（ISO 语义） */
            i++;
            while (i < n)
                dst[i++] = '\0';
            break;
        }
    }

    return dst;
}

char *catos_strchr(const char *s, int c)
{
    for (;; s++) {
        if (*s == (char)c)
            return (char *)s;
        if (*s == '\0')
            return NULL;
    }
}
