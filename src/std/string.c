#include "string.h"

#include <stdarg.h>
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

char* strcpy(char* dest, const char* src) {
    char* temp = dest;
    while ((*dest++ = *src++));
    return temp;
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

// 간단한 snprintf 구현 (기본적인 %d, %x, %s만 지원)
int snprintf(char* buffer, size_t size, const char* format, ...) {
    if (!buffer || size == 0) return 0;

    va_list args;
    va_start(args, format);

    size_t pos = 0;
    for (const char* p = format; *p && pos < size - 1; p++) {
        if (*p == '%' && *(p + 1)) {
            p++;
            switch (*p) {
                case 'd': {
                    int val = va_arg(args, int);
                    char temp[16];
                    int len = 0;

                    if (val == 0) {
                        temp[len++] = '0';
                    } else {
                        if (val < 0) {
                            if (pos < size - 1) buffer[pos++] = '-';
                            val = -val;
                        }

                        char digits[16];
                        int digit_count = 0;
                        while (val > 0) {
                            digits[digit_count++] = '0' + (val % 10);
                            val /= 10;
                        }

                        for (int i = digit_count - 1; i >= 0; i--) {
                            temp[len++] = digits[i];
                        }
                    }

                    for (int i = 0; i < len && pos < size - 1; i++) {
                        buffer[pos++] = temp[i];
                    }
                    break;
                }
                case 'x': {
                    unsigned int val = va_arg(args, unsigned int);
                    char temp[16];
                    int len = 0;

                    if (val == 0) {
                        temp[len++] = '0';
                    } else {
                        char digits[16];
                        int digit_count = 0;
                        while (val > 0) {
                            int digit = val % 16;
                            digits[digit_count++] = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
                            val /= 16;
                        }

                        for (int i = digit_count - 1; i >= 0; i--) {
                            temp[len++] = digits[i];
                        }
                    }

                    for (int i = 0; i < len && pos < size - 1; i++) {
                        buffer[pos++] = temp[i];
                    }
                    break;
                }
                case 's': {
                    const char* str = va_arg(args, const char*);
                    if (!str) str = "(null)";

                    while (*str && pos < size - 1) {
                        buffer[pos++] = *str++;
                    }
                    break;
                }
                case '%':
                    if (pos < size - 1) buffer[pos++] = '%';
                    break;
                default:
                    if (pos < size - 1) buffer[pos++] = '%';
                    if (pos < size - 1) buffer[pos++] = *p;
                    break;
            }
        } else {
            buffer[pos++] = *p;
        }
    }

    buffer[pos] = '\0';
    va_end(args);
    return pos;
}

