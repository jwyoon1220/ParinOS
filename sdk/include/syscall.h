/*
 * sdk/include/syscall.h — ParinOS SDK: System Call Interface
 *
 * Raw sysenter-based syscall stubs (implemented in sdk/lib/syscall.asm).
 * Calling convention: EAX=number, EBX=arg1, ECX=arg2, EDX=arg3.
 * Return value: EAX.
 *
 * Most programs should use the higher-level wrappers in unistd.h, stdio.h,
 * stdlib.h, and dirent.h rather than these raw functions.
 */

#ifndef PARIN_SDK_SYSCALL_H
#define PARIN_SDK_SYSCALL_H

#include <stdint.h>
#include <stddef.h>

/* ── Syscall numbers ──────────────────────────────────────────────────────── */
#define SYS_EXIT     1
#define SYS_EXEC    11
#define SYS_READ     3
#define SYS_WRITE    4
#define SYS_OPEN     5
#define SYS_CLOSE    6
#define SYS_LSEEK   19
#define SYS_GETPID  20
#define SYS_BRK     45
#define SYS_STAT   106
#define SYS_YIELD  158

/* ParinOS extensions */
#define SYS_UNLINK   10
#define SYS_MKDIR    39
#define SYS_OPENDIR 200
#define SYS_READDIR 201
#define SYS_CLOSEDIR 202

/* ── File open flags (Linux-compatible) ──────────────────────────────────── */
#define O_RDONLY  0
#define O_WRONLY  1
#define O_RDWR    2
#define O_CREAT   0x40
#define O_TRUNC   0x200
#define O_APPEND  0x400

/* ── lseek whence constants ──────────────────────────────────────────────── */
#define SEEK_SET  0
#define SEEK_CUR  1
#define SEEK_END  2

/* ── SIZE_MAX for freestanding environments ──────────────────────────────── */
#ifndef SIZE_MAX
#define SIZE_MAX ((size_t)(-1))
#endif

/* ── Raw syscall stubs (implemented in sdk/lib/syscall.asm) ─────────────── */
int syscall0(int num);
int syscall1(int num, int a1);
int syscall2(int num, int a1, int a2);
int syscall3(int num, int a1, int a2, int a3);

#endif /* PARIN_SDK_SYSCALL_H */
