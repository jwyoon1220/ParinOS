/*
 * include/stdlib.h — 유저 프로그램 표준 라이브러리
 */

#ifndef PARINOS_STDLIB_H
#define PARINOS_STDLIB_H

#include <stddef.h>
#include <stdint.h>

/* ── SIZE_MAX (freestanding 환경용) ─────────────────────────────────────── */
#ifndef SIZE_MAX
#define SIZE_MAX ((size_t)(-1))
#endif

/* ── 프로세스 제어 ───────────────────────────────────────────────────── */
void exit(int code) __attribute__((noreturn));

/* ── 동적 메모리 (정적 풀 기반) ─────────────────────────────────────── */
void *malloc(size_t size);
void  free(void *ptr);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);

/* ── 수치 변환 ───────────────────────────────────────────────────────── */
int           atoi(const char *s);
long          atol(const char *s);
unsigned long strtoul(const char *s, char **endptr, int base);
long          strtol(const char *s, char **endptr, int base);

/* ── 유틸리티 ────────────────────────────────────────────────────────── */
int abs(int x);

/* ── 최솟값 / 최댓값 매크로 ────────────────────────────────────────── */
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

/* ── NULL 정의 ─────────────────────────────────────────────────────── */
#ifndef NULL
#define NULL ((void*)0)
#endif

#endif /* PARINOS_STDLIB_H */
