/*
 * sdk/include/stdlib.h — ParinOS SDK: Standard Library
 */

#ifndef PARIN_SDK_STDLIB_H
#define PARIN_SDK_STDLIB_H

#include <stddef.h>
#include <stdint.h>

/* ── SIZE_MAX for freestanding environments ──────────────────────────────── */
#ifndef SIZE_MAX
#define SIZE_MAX ((size_t)(-1))
#endif

/* ── Process control ─────────────────────────────────────────────────────── */
void exit(int code) __attribute__((noreturn));

/* ── Dynamic memory (static-pool based) ──────────────────────────────────── */
void *malloc(size_t size);
void  free(void *ptr);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);

/* ── Numeric conversion ──────────────────────────────────────────────────── */
int           atoi(const char *s);
long          atol(const char *s);
unsigned long strtoul(const char *s, char **endptr, int base);
long          strtol(const char *s, char **endptr, int base);

/* ── Utilities ───────────────────────────────────────────────────────────── */
int abs(int x);

/* ── Min / max macros ────────────────────────────────────────────────────── */
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

/* ── NULL ────────────────────────────────────────────────────────────────── */
#ifndef NULL
#define NULL ((void*)0)
#endif

#endif /* PARIN_SDK_STDLIB_H */
