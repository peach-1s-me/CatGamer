/* ============================================================
 * CatOS 运行库 · 字符分类实现（ASCII 版）
 *
 * 与 locale 无关：同一份实现（同一字符 → 同一结果）在所有平台上成立，
 * 这是"跨平台行为一致"的必要条件。
 * ============================================================ */

#include "catos_ctype.h"

int catos_isdigit(int c)
{
    return (c >= '0' && c <= '9');
}

int catos_isxdigit(int c)
{
    return catos_isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

int catos_isupper(int c)
{
    return (c >= 'A' && c <= 'Z');
}

int catos_islower(int c)
{
    return (c >= 'a' && c <= 'z');
}

int catos_isalpha(int c)
{
    return catos_isupper(c) || catos_islower(c);
}

int catos_isalnum(int c)
{
    return catos_isalpha(c) || catos_isdigit(c);
}

/* 空白：空格、\t、\n、\v、\f、\r（ASCII） */
int catos_isspace(int c)
{
    return (c == ' ' || c == '\t' || c == '\n' ||
            c == '\v' || c == '\f' || c == '\r');
}

int catos_iscntrl(int c)
{
    return ((c >= 0x00 && c <= 0x1f) || c == 0x7f);
}

int catos_isprint(int c)
{
    return (c >= 0x20 && c <= 0x7e);
}

int catos_isgraph(int c)
{
    return (c > 0x20 && c < 0x7f);
}

int catos_ispunct(int c)
{
    return catos_isgraph(c) && !catos_isalnum(c);
}

int catos_toupper(int c)
{
    return catos_islower(c) ? c - 'a' + 'A' : c;
}

int catos_tolower(int c)
{
    return catos_isupper(c) ? c - 'A' + 'a' : c;
}
