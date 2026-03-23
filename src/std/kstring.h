//
// src/std/kstring.h — 커널 문자열 함수
//

#ifndef PARINOS_KSTRING_H
#define PARINOS_KSTRING_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 문자열 비교 함수 */
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, int n);

/* 문자열 길이 확인 */
int strlen(const char *s);

/* 문자열 결합 함수 */
char* strcat(char *dest, const char *src);
char* strncat(char *dest, const char *src, int n);

char* strcpy(char* dest, const char* src);

char* strchr(const char *s, int c);
uint32_t atoi_hex(const char *s);

// 새로 추가
int snprintf(char* buffer, size_t size, const char* format, ...);

#ifdef __cplusplus
}
#endif

#endif /* PARINOS_KSTRING_H */
