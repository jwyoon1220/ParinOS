/*
 * sdk/include/string.h — ParinOS SDK: String and Memory Functions
 */

#ifndef PARIN_SDK_STRING_H
#define PARIN_SDK_STRING_H

#include <stddef.h>
#include <stdint.h>

/* ── String length / copy / concatenate ─────────────────────────────────── */
size_t strlen(const char *s);
char  *strcpy(char *dst, const char *src);
char  *strncpy(char *dst, const char *src, size_t n);
char  *strcat(char *dst, const char *src);
char  *strncat(char *dst, const char *src, size_t n);

/* ── String comparison ───────────────────────────────────────────────────── */
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);

/* ── String search ───────────────────────────────────────────────────────── */
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);

/* ── Memory operations ───────────────────────────────────────────────────── */
void *memset(void *dst, int c, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
int   memcmp(const void *s1, const void *s2, size_t n);

/* ── Token splitting ─────────────────────────────────────────────────────── */
char *strtok(char *s, const char *delim);

#endif /* PARIN_SDK_STRING_H */
