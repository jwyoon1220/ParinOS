/*
 * sdk/include/sys/stat.h — ParinOS SDK: File Status
 *
 * Provides the stat() system call (SYS_STAT = 106).
 * The kernel fills: st_size (file size in bytes) and st_mode (type bits).
 *
 * st_mode bit patterns:
 *   0x8000  Regular file  (S_IFREG)
 *   0x4000  Directory     (S_IFDIR)
 */

#ifndef PARIN_SDK_SYS_STAT_H
#define PARIN_SDK_SYS_STAT_H

#include <stdint.h>
#include "../syscall.h"

/* ── File type bit masks ─────────────────────────────────────────────────── */
#define S_IFREG  0x8000   /* Regular file */
#define S_IFDIR  0x4000   /* Directory */
#define S_IFMT   0xF000   /* File type mask */

#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)

/* ── stat structure ──────────────────────────────────────────────────────── */
struct stat {
    uint32_t st_size;   /* Total size in bytes */
    uint16_t st_mode;   /* File type (S_IFREG / S_IFDIR) */
};

/* ── stat() wrapper ──────────────────────────────────────────────────────── */
static inline int stat(const char *path, struct stat *buf) {
    return syscall2(SYS_STAT, (int)path, (int)buf);
}

#endif /* PARIN_SDK_SYS_STAT_H */
