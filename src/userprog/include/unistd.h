/*
 * unistd.h — POSIX 저수준 I/O 인터페이스 (유저 프로그램용)
 *
 * 모든 함수는 int 0x80 시스템 콜을 통해 커널과 통신합니다.
 */

#ifndef USERPROG_UNISTD_H
#define USERPROG_UNISTD_H

#include <stddef.h>
#include <stdint.h>
#include "syscall.h"

/* ── I/O ──────────────────────────────────────────────────────────────── */

static inline int write(int fd, const void *buf, int count) {
    return syscall3(SYS_WRITE, fd, (int)buf, count);
}

static inline int read(int fd, void *buf, int count) {
    return syscall3(SYS_READ, fd, (int)buf, count);
}

static inline int open(const char *path, int flags, int mode) {
    return syscall3(SYS_OPEN, (int)path, flags, mode);
}

static inline int close(int fd) {
    return syscall1(SYS_CLOSE, fd);
}

static inline int lseek(int fd, int offset, int whence) {
    return syscall3(SYS_LSEEK, fd, offset, whence);
}

/* ── 프로세스 ────────────────────────────────────────────────────────── */

static inline int getpid(void) {
    return syscall0(SYS_GETPID);
}

static inline void _exit(int code) {
    syscall1(SYS_EXIT, code);
    /* 도달하지 않음 */
    while (1) {}
}

static inline int execve(const char *path, int argc, const char **argv) {
    return syscall3(SYS_EXEC, (int)path, argc, (int)argv);
}

static inline void sched_yield(void) {
    syscall0(SYS_YIELD);
}

#endif /* USERPROG_UNISTD_H */
