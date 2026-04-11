/*
 * sdk/include/parin/io.h — ParinOS SDK: I/O Utilities
 *
 * Higher-level I/O helpers built on top of unistd.h.
 * Implemented in sdk/lib/parin_io.c (compiled into libparin.a).
 */

#ifndef PARIN_IO_H
#define PARIN_IO_H

#include <stddef.h>

/* ── Reliable write / read ───────────────────────────────────────────────── */

/**
 * Write exactly size bytes from buf to fd, retrying on short writes.
 *
 * Returns size on success, or -1 if an error occurs before all bytes are sent.
 */
int io_write_all(int fd, const void *buf, int size);

/**
 * Read exactly size bytes from fd into buf.
 * Blocks until all bytes are received or EOF / error.
 *
 * Returns the number of bytes actually read (may be < size on EOF).
 * Returns -1 on error.
 */
int io_read_all(int fd, void *buf, int size);

/* ── Line-oriented I/O ────────────────────────────────────────────────────── */

/**
 * Read one line from fd into buf (at most max-1 characters + '\0').
 * The trailing newline is stored in buf if it fits.
 *
 * Returns the number of characters read (0 on EOF, -1 on error).
 */
int io_read_line(int fd, char *buf, int max);

/* ── Convenience print helpers ────────────────────────────────────────────── */

/**
 * Write the null-terminated string s followed by '\n' to fd.
 * Returns the total number of bytes written, or -1 on error.
 */
int io_println(int fd, const char *s);

/**
 * Write the null-terminated string s to fd (no newline appended).
 * Returns the number of bytes written, or -1 on error.
 */
int io_print(int fd, const char *s);

#endif /* PARIN_IO_H */
