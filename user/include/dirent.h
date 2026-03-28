/*
 * include/dirent.h — 디렉터리 엔트리 인터페이스 (유저 프로그램용)
 */

#ifndef PARINOS_DIRENT_H
#define PARINOS_DIRENT_H

#include <stdint.h>
#include "syscall.h"

/* ── 파일 종류 ──────────────────────────────────────────────────────── */
#define DT_REG  1   /* 일반 파일 */
#define DT_DIR  2   /* 디렉터리 */

/* ── 디렉터리 엔트리 구조체 ─────────────────────────────────────────── */
struct dirent {
    char     d_name[256];
    uint32_t d_size;
    uint8_t  d_type;
};

/* ── 디렉터리 함수 ──────────────────────────────────────────────────── */

static inline int opendir_fd(const char *path) {
    return syscall1(SYS_OPENDIR, (int)path);
}

static inline int readdir_r(int fd, struct dirent *ent) {
    return syscall2(SYS_READDIR, fd, (int)ent);
}

static inline void closedir_fd(int fd) {
    syscall1(SYS_CLOSEDIR, fd);
}

#endif /* PARINOS_DIRENT_H */
