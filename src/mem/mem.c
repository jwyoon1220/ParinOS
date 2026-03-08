//
// Created by jwyoo on 26. 3. 8..
//

#include "mem.h"

// 1. memset: 메모리 초기화
void* memset(void* dest, int ch, size_t count) {
    uint8_t* p = (uint8_t*)dest;
    while (count--) {
        *p++ = (uint8_t)ch;
    }
    return dest;
}

// 2. memcpy: 빠른 메모리 복사
void* memcpy(void* dest, const void* src, size_t count) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    while (count--) {
        *d++ = *s++;
    }
    return dest;
}

// 3. memmove: 겹침 방지 복사 (매우 중요)
void* memmove(void* dest, const void* src, size_t count) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;

    if (d < s) {
        // 앞쪽에서부터 복사
        while (count--) *d++ = *s++;
    } else {
        // 뒤쪽에서부터 복사 (Destination이 Source보다 뒤에 있을 때 오버라이트 방지)
        d += count;
        s += count;
        while (count--) *--d = *--s;
    }
    return dest;
}

// 4. memcmp: 메모리 비교
int memcmp(const void* lhs, const void* rhs, size_t count) {
    const uint8_t* l = (const uint8_t*)lhs;
    const uint8_t* r = (const uint8_t*)rhs;

    for (size_t i = 0; i < count; i++) {
        if (l[i] < r[i]) return -1;
        if (l[i] > r[i]) return 1;
    }
    return 0;
}