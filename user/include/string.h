/*
 * include/string.h — 유저 프로그램 문자열 함수
 */

#ifndef PARINOS_STRING_H
#define PARINOS_STRING_H

#include <stddef.h>
#include <stdint.h>

/* ── 문자열 길이 / 복사 / 결합 ─────────────────────────────────────── */
size_t strlen(const char *s);
char  *strcpy(char *dst, const char *src);
char  *strncpy(char *dst, const char *src, size_t n);
char  *strcat(char *dst, const char *src);
char  *strncat(char *dst, const char *src, size_t n);

/* ── 문자열 비교 ─────────────────────────────────────────────────────── */
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);

/* ── 문자 탐색 ───────────────────────────────────────────────────────── */
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);

/* ── 메모리 조작 ─────────────────────────────────────────────────────── */
void *memset(void *dst, int c, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
int   memcmp(const void *s1, const void *s2, size_t n);

/* ── 토큰 분리 ───────────────────────────────────────────────────────── */
char *strtok(char *s, const char *delim);

#endif /* PARINOS_STRING_H */
