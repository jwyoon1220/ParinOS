/*
 * sdk/include/stdio.h — ParinOS SDK: Standard I/O
 *
 * FILE*-based buffered I/O and printf family functions.
 */

#ifndef PARIN_SDK_STDIO_H
#define PARIN_SDK_STDIO_H

#include <stddef.h>
#include <stdarg.h>
#include "syscall.h"

/* ── Standard file descriptor numbers ───────────────────────────────────── */
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/* ── FILE type ───────────────────────────────────────────────────────────── */
#define FILE_BUF_SIZE 512

typedef struct {
    int  fd;
    char buf[FILE_BUF_SIZE];
    int  buf_pos;
    int  buf_len;
    int  eof;
    int  err;
    int  unget_char;  /* -1 = none; >= 0 = pushed-back character */
} FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

/* ── File open / close ───────────────────────────────────────────────────── */
FILE *fopen(const char *path, const char *mode);
int   fclose(FILE *f);

/* ── Formatted output ────────────────────────────────────────────────────── */
int printf(const char *fmt, ...);
int fprintf(FILE *f, const char *fmt, ...);
int sprintf(char *buf, const char *fmt, ...);
int snprintf(char *buf, size_t size, const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);
int vfprintf(FILE *f, const char *fmt, va_list ap);
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);

/* ── Character I/O ───────────────────────────────────────────────────────── */
int putchar(int c);
int puts(const char *s);
int fputc(int c, FILE *f);
int fputs(const char *s, FILE *f);

int getchar(void);
int fgetc(FILE *f);
char *fgets(char *buf, int size, FILE *f);

/* ── Block I/O ───────────────────────────────────────────────────────────── */
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *f);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *f);

/* ── Misc ────────────────────────────────────────────────────────────────── */
int feof(FILE *f);
int ferror(FILE *f);
void clearerr(FILE *f);
int fseek(FILE *f, long offset, int whence);
long ftell(FILE *f);

void perror(const char *s);

/* ── Formatted input ─────────────────────────────────────────────────────── */
int scanf(const char *fmt, ...);
int fscanf(FILE *f, const char *fmt, ...);
int sscanf(const char *str, const char *fmt, ...);
int vscanf(const char *fmt, va_list ap);
int vfscanf(FILE *f, const char *fmt, va_list ap);
int vsscanf(const char *str, const char *fmt, va_list ap);

/* ── Push-back / rewind ──────────────────────────────────────────────────── */
int  ungetc(int c, FILE *f);
void rewind(FILE *f);

/* ── Safe line input ─────────────────────────────────────────────────────── */

/**
 * Read one line from stdin into buf (at most size-1 chars).
 * Strips the trailing newline.  Safer replacement for gets().
 */
char *gets_s(char *buf, int size);

#endif /* PARIN_SDK_STDIO_H */
