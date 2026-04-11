/*
 * sdk/include/dirent.h — ParinOS SDK: Directory Entry Interface
 */

#ifndef PARIN_SDK_DIRENT_H
#define PARIN_SDK_DIRENT_H

#include <stdint.h>
#include "syscall.h"

/* ── File type constants ─────────────────────────────────────────────────── */
#define DT_REG  1   /* Regular file */
#define DT_DIR  2   /* Directory */

/* ── Directory entry ─────────────────────────────────────────────────────── */
struct dirent {
    char     d_name[256];
    uint32_t d_size;
    uint8_t  d_type;  /* DT_REG or DT_DIR */
};

/* ── Directory functions ─────────────────────────────────────────────────── */

static inline int opendir_fd(const char *path) {
    return syscall1(SYS_OPENDIR, (int)path);
}

static inline int readdir_r(int fd, struct dirent *ent) {
    return syscall2(SYS_READDIR, fd, (int)ent);
}

static inline void closedir_fd(int fd) {
    syscall1(SYS_CLOSEDIR, fd);
}

#endif /* PARIN_SDK_DIRENT_H */
