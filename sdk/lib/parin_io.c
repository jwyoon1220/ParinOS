/*
 * sdk/lib/parin_io.c — ParinOS SDK: I/O Utilities
 */

#include "parin/io.h"
#include "unistd.h"
#include "string.h"

/* ── Reliable write ─────────────────────────────────────────────────────── */

int io_write_all(int fd, const void *buf, int size) {
    const char *p = (const char*)buf;
    int remaining = size;
    while (remaining > 0) {
        int n = write(fd, p, remaining);
        if (n <= 0) return -1;
        p         += n;
        remaining -= n;
    }
    return size;
}

/* ── Reliable read ──────────────────────────────────────────────────────── */

int io_read_all(int fd, void *buf, int size) {
    char *p = (char*)buf;
    int total = 0;
    while (total < size) {
        int n = read(fd, p + total, size - total);
        if (n < 0) return -1;
        if (n == 0) break;   /* EOF */
        total += n;
    }
    return total;
}

/* ── Line-oriented read ─────────────────────────────────────────────────── */

int io_read_line(int fd, char *buf, int max) {
    if (!buf || max <= 0) return -1;
    int i = 0;
    while (i < max - 1) {
        char c;
        int n = read(fd, &c, 1);
        if (n < 0) return -1;
        if (n == 0) break;   /* EOF */
        buf[i++] = c;
        if (c == '\n') break;
    }
    buf[i] = '\0';
    return i;
}

/* ── Convenience print helpers ──────────────────────────────────────────── */

int io_print(int fd, const char *s) {
    int len = (int)strlen(s);
    return io_write_all(fd, s, len);
}

int io_println(int fd, const char *s) {
    int n = io_print(fd, s);
    if (n < 0) return -1;
    int r = io_write_all(fd, "\n", 1);
    if (r < 0) return -1;
    return n + 1;
}
