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

/* ── Searching and sorting ───────────────────────────────────────────────── */

/**
 * Sort an array of nmemb elements, each of size bytes, using compar
 * to compare two elements.  In-place quicksort with insertion sort for
 * small partitions.
 */
void qsort(void *base, size_t nmemb, size_t size,
           int (*compar)(const void *, const void *));

/**
 * Binary search for key in a sorted array.
 * Returns a pointer to the matching element, or NULL if not found.
 */
void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compar)(const void *, const void *));

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
