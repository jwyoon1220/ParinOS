/*
 * sdk/lib/parin_fs.c — ParinOS SDK: Filesystem Utilities
 */

#include "parin/fs.h"
#include "sys/stat.h"
#include "unistd.h"
#include "stdlib.h"
#include "string.h"

/* ── Existence / type tests ─────────────────────────────────────────────── */

int fs_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

int fs_is_file(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return S_ISREG(st.st_mode);
}

int fs_is_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return S_ISDIR(st.st_mode);
}

/* ── Metadata ───────────────────────────────────────────────────────────── */

int fs_size(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (int)st.st_size;
}

/* ── Reading ────────────────────────────────────────────────────────────── */

char *fs_read_all(const char *path, int *out_size) {
    int fd = open(path, O_RDONLY, 0);
    if (fd < 0) return (char*)0;

    /* Determine file size via lseek */
    int size = lseek(fd, 0, SEEK_END);
    if (size < 0) { close(fd); return (char*)0; }
    lseek(fd, 0, SEEK_SET);

    /* Allocate buffer (size + 1 for null terminator) */
    char *buf = (char*)malloc((size_t)size + 1);
    if (!buf) { close(fd); return (char*)0; }

    int total = 0;
    while (total < size) {
        int n = read(fd, buf + total, size - total);
        if (n <= 0) break;
        total += n;
    }
    buf[total] = '\0';
    close(fd);

    if (out_size) *out_size = total;
    return buf;
}

/* ── Writing ────────────────────────────────────────────────────────────── */

int fs_write_all(const char *path, const void *data, int size) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;

    int total = 0;
    const char *p = (const char*)data;
    while (total < size) {
        int n = write(fd, p + total, size - total);
        if (n <= 0) { close(fd); return -1; }
        total += n;
    }
    close(fd);
    return total;
}

int fs_append(const char *path, const void *data, int size) {
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) return -1;

    int total = 0;
    const char *p = (const char*)data;
    while (total < size) {
        int n = write(fd, p + total, size - total);
        if (n <= 0) { close(fd); return -1; }
        total += n;
    }
    close(fd);
    return total;
}

/* ── Copying / removing ─────────────────────────────────────────────────── */

#define FS_COPY_BUF 512

int fs_copy(const char *src, const char *dst) {
    int src_fd = open(src, O_RDONLY, 0);
    if (src_fd < 0) return -1;

    int dst_fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst_fd < 0) { close(src_fd); return -1; }

    char buf[FS_COPY_BUF];
    int ok = 0;
    int n;
    while ((n = read(src_fd, buf, FS_COPY_BUF)) > 0) {
        int written = 0;
        while (written < n) {
            int w = write(dst_fd, buf + written, n - written);
            if (w <= 0) { ok = -1; goto done; }
            written += w;
        }
    }
done:
    close(src_fd);
    close(dst_fd);
    return ok;
}

int fs_remove(const char *path) {
    return unlink(path);
}

/* ── Directory creation ─────────────────────────────────────────────────── */

int fs_mkdir(const char *path, int mode) {
    if (fs_is_dir(path)) return 0;   /* Already exists */
    return mkdir(path, mode);
}
