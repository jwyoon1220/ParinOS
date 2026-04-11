/*
 * sdk/include/unistd.h — ParinOS SDK: POSIX-style low-level I/O
 *
 * Thin inline wrappers around sysenter syscalls.
 */

#ifndef PARIN_SDK_UNISTD_H
#define PARIN_SDK_UNISTD_H

#include <stddef.h>
#include <stdint.h>
#include "syscall.h"

/* ── Standard file descriptor numbers ───────────────────────────────────── */
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/* ── I/O ─────────────────────────────────────────────────────────────────── */

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

/* ── Filesystem ──────────────────────────────────────────────────────────── */

static inline int unlink(const char *path) {
    return syscall1(SYS_UNLINK, (int)path);
}

static inline int mkdir(const char *path, int mode) {
    return syscall2(SYS_MKDIR, (int)path, mode);
}

/* ── Process ─────────────────────────────────────────────────────────────── */

static inline int getpid(void) {
    return syscall0(SYS_GETPID);
}

static inline void _exit(int code) {
    syscall1(SYS_EXIT, code);
    while (1) {}
}

static inline int execve(const char *path, int argc, const char **argv) {
    return syscall3(SYS_EXEC, (int)path, argc, (int)argv);
}

static inline void sched_yield(void) {
    syscall0(SYS_YIELD);
}

#endif /* PARIN_SDK_UNISTD_H */
