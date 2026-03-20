//
// src/std/kstdlib.c — 커널 표준 라이브러리 구현
//

#include "kstdlib.h"
#include "../mem/mem.h"
#include "../vga.h"

/* ─────────────────────────────────────────────────────────────────────────────
 * 숫자 ↔ 문자열 변환
 * ───────────────────────────────────────────────────────────────────────────*/

int katoi(const char *s) {
    int sign = 1, result = 0;
    while (*s == ' ' || *s == '\t' || *s == '\n') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') { s++; }
    while (*s >= '0' && *s <= '9') {
        result = result * 10 + (*s - '0');
        s++;
    }
    return sign * result;
}

long katol(const char *s) {
    long sign = 1, result = 0;
    while (*s == ' ' || *s == '\t' || *s == '\n') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') { s++; }
    while (*s >= '0' && *s <= '9') {
        result = result * 10 + (*s - '0');
        s++;
    }
    return sign * result;
}

double katof(const char *s) {
    double sign = 1.0, integer = 0.0, frac = 0.0, frac_div = 1.0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { sign = -1.0; s++; }
    else if (*s == '+') { s++; }
    while (*s >= '0' && *s <= '9') {
        integer = integer * 10.0 + (double)(*s - '0');
        s++;
    }
    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9') {
            frac      = frac * 10.0 + (double)(*s - '0');
            frac_div *= 10.0;
            s++;
        }
    }
    /* 지수 표기 (간단 처리) */
    if (*s == 'e' || *s == 'E') {
        s++;
        int esign = 1, exp = 0;
        if (*s == '-') { esign = -1; s++; }
        else if (*s == '+') { s++; }
        while (*s >= '0' && *s <= '9') { exp = exp * 10 + (*s - '0'); s++; }
        double mult = 1.0;
        for (int i = 0; i < exp; i++) mult *= 10.0;
        if (esign < 0) mult = 1.0 / mult;
        return sign * (integer + frac / frac_div) * mult;
    }
    return sign * (integer + frac / frac_div);
}

char *kitoa(int value, char *buf, int base) {
    if (base < 2 || base > 36) { buf[0] = '\0'; return buf; }
    char  tmp[34];
    int   i = 0;
    int   neg = 0;

    if (value == 0) { buf[0] = '0'; buf[1] = '\0'; return buf; }
    if (value < 0 && base == 10) { neg = 1; value = -value; }

    unsigned int uval = (unsigned int)value;
    while (uval > 0) {
        int digit = (int)(uval % (unsigned int)base);
        tmp[i++]  = (char)(digit < 10 ? '0' + digit : 'a' + digit - 10);
        uval /= (unsigned int)base;
    }
    if (neg) tmp[i++] = '-';

    /* 반전 */
    int k;
    for (k = 0; k < i; k++) buf[k] = tmp[i - 1 - k];
    buf[k] = '\0';
    return buf;
}

char *kutoa(unsigned int value, char *buf, int base) {
    if (base < 2 || base > 36) { buf[0] = '\0'; return buf; }
    char tmp[34];
    int  i = 0;

    if (value == 0) { buf[0] = '0'; buf[1] = '\0'; return buf; }

    while (value > 0) {
        int digit = (int)(value % (unsigned int)base);
        tmp[i++]  = (char)(digit < 10 ? '0' + digit : 'a' + digit - 10);
        value /= (unsigned int)base;
    }

    int k;
    for (k = 0; k < i; k++) buf[k] = tmp[i - 1 - k];
    buf[k] = '\0';
    return buf;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * 유사 난수 (선형 합동법, Knuth MMIX 계수)
 * ───────────────────────────────────────────────────────────────────────────*/
static uint64_t g_rand_state = 12345;

void ksrand(uint32_t seed) {
    g_rand_state = (uint64_t)seed;
}

int krand(void) {
    g_rand_state = g_rand_state * 6364136223846793005ULL + 1442695040888963407ULL;
    return (int)((g_rand_state >> 33) & (uint64_t)KRAND_MAX);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * 정렬: 비재귀 quicksort
 * ───────────────────────────────────────────────────────────────────────────*/

/* 원소 두 개 교환 */
static void swap_bytes(uint8_t *a, uint8_t *b, size_t size) {
    while (size--) {
        uint8_t t = *a;
        *a++ = *b;
        *b++ = t;
    }
}

void kqsort(void *base, size_t nmemb, size_t size,
            int (*compar)(const void *, const void *)) {
    if (nmemb <= 1 || size == 0) return;

    /* 반복적 quicksort: 스택 최대 64 수준 */
    struct { size_t lo; size_t hi; } stack[64];
    int top = 0;

    stack[top].lo = 0;
    stack[top].hi = nmemb - 1;

    while (top >= 0) {
        size_t lo = stack[top].lo;
        size_t hi = stack[top].hi;
        top--;

        if (lo >= hi) continue;

        /* 중앙값 피벗 선택 */
        size_t mid = lo + (hi - lo) / 2;
        uint8_t *p_lo  = (uint8_t *)base + lo  * size;
        uint8_t *p_mid = (uint8_t *)base + mid * size;
        uint8_t *p_hi  = (uint8_t *)base + hi  * size;

        /* 정렬: lo, mid, hi 세 값을 정리 */
        if (compar(p_lo, p_mid) > 0) swap_bytes(p_lo, p_mid, size);
        if (compar(p_lo, p_hi)  > 0) swap_bytes(p_lo, p_hi,  size);
        if (compar(p_mid, p_hi) > 0) swap_bytes(p_mid, p_hi, size);

        /* 피벗을 hi-1 위치로 이동 */
        if (hi - lo > 1) {
            uint8_t *p_hi1 = (uint8_t *)base + (hi - 1) * size;
            swap_bytes(p_mid, p_hi1, size);

            uint8_t *pivot = p_hi1;
            size_t i = lo;
            size_t j = hi - 1;

            while (1) {
                do { i++; } while (compar((uint8_t *)base + i * size, pivot) < 0);
                do { j--; } while (j > lo && compar((uint8_t *)base + j * size, pivot) > 0);
                if (i >= j) break;
                swap_bytes((uint8_t *)base + i * size,
                           (uint8_t *)base + j * size, size);
            }

            /* 피벗 복원 */
            swap_bytes((uint8_t *)base + i * size, pivot, size);

            /* 서브 구간 스택에 추가 */
            if (top < 62) {
                if (i > lo + 1) { top++; stack[top].lo = lo;  stack[top].hi = i - 1; }
                if (i < hi - 1) { top++; stack[top].lo = i + 1; stack[top].hi = hi; }
            }
        }
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * 이진 탐색
 * ───────────────────────────────────────────────────────────────────────────*/
void *kbsearch(const void *key, const void *base,
               size_t nmemb, size_t size,
               int (*compar)(const void *, const void *)) {
    size_t lo = 0, hi = nmemb;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const void *elem = (const uint8_t *)base + mid * size;
        int cmp = compar(key, elem);
        if (cmp == 0) return (void *)elem;
        if (cmp > 0)  lo = mid + 1;
        else          hi = mid;
    }
    return NULL;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * 환경
 * ───────────────────────────────────────────────────────────────────────────*/
void kabort(void) {
    klog_error("[kstdlib] kabort() called — kernel halt\n");
    asm volatile("cli; hlt");
    /* 복귀하지 않음 */
    while (1) {}
}

int katexit(void (*func)(void)) {
    (void)func;
    return 0; /* 커널에서는 no-op */
}
