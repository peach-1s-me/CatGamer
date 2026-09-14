#ifndef CATOS_CTYPE_H
#define CATOS_CTYPE_H

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * CatOS 运行库 · 字符分类（对应 ISO <ctype.h>）
 *
 * 首版为 ASCII 版（与 lconv/locale 无关），行为在所有平台上一致。
 * 待扩充：本地化、宽字符（wctype）。
 * ============================================================ */

int catos_isdigit (int c);
int catos_isxdigit(int c);
int catos_isalpha (int c);
int catos_isalnum (int c);
int catos_isspace (int c);
int catos_isupper (int c);
int catos_islower (int c);
int catos_ispunct (int c);
int catos_isprint (int c);
int catos_isgraph (int c);
int catos_iscntrl (int c);

int catos_toupper (int c);
int catos_tolower (int c);

#ifdef __cplusplus
}
#endif

#endif /* CATOS_CTYPE_H */
