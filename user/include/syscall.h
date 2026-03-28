/*
 * include/syscall.h — ParinOS 유저 프로그램 시스템 콜 인터페이스
 *
 * sysenter 기반 시스템 콜 래퍼 (어셈블리 구현: user/lib/syscall.asm)
 * 호출 규약: EAX=번호, EBX=arg1, ECX=arg2, EDX=arg3
 * 반환값: EAX
 */

#ifndef PARINOS_SYSCALL_H
#define PARINOS_SYSCALL_H

#include <stdint.h>

/* ── 시스템 콜 번호 ─────────────────────────────────────────────────────── */
#define SYS_EXIT    1
#define SYS_EXEC    11
#define SYS_READ    3
#define SYS_WRITE   4
#define SYS_OPEN    5
#define SYS_CLOSE   6
#define SYS_LSEEK   19
#define SYS_GETPID  20
#define SYS_BRK     45
#define SYS_STAT    106
#define SYS_YIELD   158

/* ParinOS 확장 시스템 콜 */
#define SYS_UNLINK   10
#define SYS_MKDIR    39
#define SYS_OPENDIR  200
#define SYS_READDIR  201
#define SYS_CLOSEDIR 202

/* ── 파일 열기 플래그 (Linux 호환) ─────────────────────────────────────── */
#define O_RDONLY    0
#define O_WRONLY    1
#define O_RDWR      2
#define O_CREAT     0x40
#define O_TRUNC     0x200
#define O_APPEND    0x400

/* ── SEEK 기준 ──────────────────────────────────────────────────────────── */
#define SEEK_SET  0
#define SEEK_CUR  1
#define SEEK_END  2

/* ── SIZE_MAX (freestanding 환경용) ─────────────────────────────────────── */
#include <stddef.h>
#ifndef SIZE_MAX
#define SIZE_MAX ((size_t)(-1))
#endif

/* ── 원시 시스템 콜 함수 선언 (user/lib/syscall.asm 에서 구현) ─────────── */
int syscall0(int num);
int syscall1(int num, int a1);
int syscall2(int num, int a1, int a2);
int syscall3(int num, int a1, int a2, int a3);

#endif /* PARINOS_SYSCALL_H */
