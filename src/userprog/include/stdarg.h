/*
 * stdarg.h — 유저 프로그램용 가변 인수 매크로
 */
#ifndef _STDARG_H
#define _STDARG_H

typedef __builtin_va_list va_list;

#define va_start(ap, last)  __builtin_va_start(ap, last)
#define va_end(ap)          __builtin_va_end(ap)
#define va_arg(ap, type)    __builtin_va_arg(ap, type)
#define va_copy(dst, src)   __builtin_va_copy(dst, src)

#endif /* _STDARG_H */
