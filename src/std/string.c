#include "string.h"

#include <stddef.h>
#include <stdint.h>

// 문자열의 길이를 반환
int strlen(const char *s) {
    int len = 0;
    while (s[len] != '\0') {
        len++;
    }
    return len;
}

// 두 문자열이 같은지 비교
int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

// n바이트만큼 두 문자열을 비교
int strncmp(const char *s1, const char *s2, int n) {
    if (n == 0) return 0;

    while (n > 0 && *s1 && (*s1 == *s2)) {
        if (n == 1) return 0;
        s1++;
        s2++;
        n--;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

// 🌟 수정한 부분: const char *src로 변경하여 헤더와 일치시킴
char* strcat(char *dest, const char *src) {
    char *ptr = dest;

    // 1. dest의 끝('\0')까지 이동
    while (*ptr != '\0') {
        ptr++;
    }

    // 2. 그 위치부터 src를 하나씩 복사
    while (*src != '\0') {
        *ptr++ = *src++;
    }

    // 3. 마지막에 널 문자로 닫기
    *ptr = '\0';

    return dest;
}

// 최대 n개까지만 src를 dest 뒤에 붙임
char* strncat(char *dest, const char *src, int n) {
    char *ptr = dest;

    while (*ptr != '\0') {
        ptr++;
    }

    while (n > 0 && *src != '\0') {
        *ptr++ = *src++;
        n--;
    }

    *ptr = '\0';

    return dest;
}

uint32_t atoi_hex(const char *s) {
    uint32_t res = 0;
    while (*s) {
        res <<= 4;
        if (*s >= '0' && *s <= '9') res += (*s - '0');
        else if (*s >= 'a' && *s <= 'f') res += (*s - 'a' + 10);
        else if (*s >= 'A' && *s <= 'F') res += (*s - 'A' + 10);
        s++;
    }
    return res;
}
// 특정 문자의 위치를 찾는 함수
char* strchr(const char *s, int c) {
    while (*s != (char)c) {
        if (!*s++) return NULL;
    }
    return (char *)s;
}

