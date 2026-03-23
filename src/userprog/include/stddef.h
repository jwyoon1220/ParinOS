/*
 * stddef.h — 유저 프로그램용 최소 정의
 */
#ifndef _STDDEF_H
#define _STDDEF_H

typedef unsigned int   size_t;
typedef int            ptrdiff_t;
typedef unsigned int   uintptr_t;

#ifndef NULL
#define NULL ((void*)0)
#endif

#define offsetof(type, member) __builtin_offsetof(type, member)

#endif /* _STDDEF_H */
