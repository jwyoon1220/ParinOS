/*
 * include/stdio.h — 유저 프로그램 표준 입출력
 *
 * FILE* 기반 버퍼 I/O와 printf 제공.
 */

#ifndef PARINOS_STDIO_H
#define PARINOS_STDIO_H

#include <stddef.h>
#include <stdarg.h>
#include "syscall.h"

/* ── 파일 디스크립터 상수 ─────────────────────────────────────────────── */
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/* ── FILE 타입 ───────────────────────────────────────────────────────── */
#define FILE_BUF_SIZE 512

typedef struct {
    int  fd;
    char buf[FILE_BUF_SIZE];
    int  buf_pos;
    int  buf_len;
    int  eof;
    int  err;
} FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

/* ── 파일 열기 / 닫기 ────────────────────────────────────────────────── */
FILE *fopen(const char *path, const char *mode);
int   fclose(FILE *f);

/* ── 포맷 출력 ───────────────────────────────────────────────────────── */
int printf(const char *fmt, ...);
int fprintf(FILE *f, const char *fmt, ...);
int sprintf(char *buf, const char *fmt, ...);
int snprintf(char *buf, size_t size, const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);
int vfprintf(FILE *f, const char *fmt, va_list ap);
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);

/* ── 문자 I/O ────────────────────────────────────────────────────────── */
int putchar(int c);
int puts(const char *s);
int fputc(int c, FILE *f);
int fputs(const char *s, FILE *f);

int getchar(void);
int fgetc(FILE *f);
char *fgets(char *buf, int size, FILE *f);

/* ── 블록 I/O ────────────────────────────────────────────────────────── */
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *f);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *f);

/* ── 기타 ────────────────────────────────────────────────────────────── */
int feof(FILE *f);
int ferror(FILE *f);
void clearerr(FILE *f);
int fseek(FILE *f, long offset, int whence);
long ftell(FILE *f);

void perror(const char *s);

#endif /* PARINOS_STDIO_H */
