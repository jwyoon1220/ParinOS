//
// Created by jwyoo on 26. 3. 7..
//

#ifndef PARINOS_STRING_H
#define PARINOS_STRING_H

#include <stdint.h>

/* 문자열 비교 함수 */
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, int n);

/* 문자열 길이 확인 */
int strlen(const char *s);

/* 문자열 결합 함수 (🌟 src에 const 추가) */
char* strcat(char *dest, const char *src);
char* strncat(char *dest, const char *src, int n);


char* strchr(const char *s, int c);
uint32_t atoi_hex(const char *s);

#endif //PARINOS_STRING_H