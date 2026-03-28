/*
 * stdlib.c — 유저 프로그램 표준 라이브러리 구현
 *
 * malloc/free: 정적 힙 풀 기반 (단순 First-Fit 알고리즘)
 */

#include "stdlib.h"
#include "syscall.h"
#include "string.h"

/* ── exit ────────────────────────────────────────────────────────────── */
void exit(int code) {
    syscall1(SYS_EXIT, code);
    while (1) {}
}

/* ── 정적 힙 (64KB) ────────────────────────────────────────────────── */
#define HEAP_SIZE (64 * 1024)

static unsigned char g_heap[HEAP_SIZE];
static int           g_heap_init = 0;

/* 블록 헤더: 크기(uint32_t) + 사용 여부(uint32_t) */
typedef struct block_hdr {
    unsigned int  size;   /* 헤더 제외 페이로드 크기 */
    unsigned int  used;   /* 1 = 사용 중, 0 = 빈 블록 */
} block_hdr_t;

#define HDR_SIZE  sizeof(block_hdr_t)

static void heap_init(void) {
    block_hdr_t *hdr = (block_hdr_t*)g_heap;
    hdr->size = HEAP_SIZE - HDR_SIZE;
    hdr->used = 0;
    g_heap_init = 1;
}

void *malloc(size_t size) {
    if (!g_heap_init) heap_init();
    if (size == 0) return (void*)0;

    /* 4바이트 정렬 */
    size = (size + 3) & ~3u;

    unsigned char *p = g_heap;
    unsigned char *end = g_heap + HEAP_SIZE;

    while (p + HDR_SIZE <= end) {
        block_hdr_t *hdr = (block_hdr_t*)p;
        if (!hdr->used && hdr->size >= size) {
            /* 블록 분할: 남은 공간이 헤더 + 최소 4바이트 이상이면 분할 */
            if (hdr->size >= size + HDR_SIZE + 4) {
                block_hdr_t *next = (block_hdr_t*)(p + HDR_SIZE + size);
                next->size = hdr->size - size - HDR_SIZE;
                next->used = 0;
                hdr->size  = size;
            }
            hdr->used = 1;
            return (void*)(p + HDR_SIZE);
        }
        p += HDR_SIZE + hdr->size;
    }
    return (void*)0; /* 메모리 부족 */
}

void free(void *ptr) {
    if (!ptr) return;
    block_hdr_t *hdr = (block_hdr_t*)((unsigned char*)ptr - HDR_SIZE);
    hdr->used = 0;

    /* 인접 빈 블록 병합 */
    unsigned char *p = g_heap;
    unsigned char *end = g_heap + HEAP_SIZE;
    while (p + HDR_SIZE <= end) {
        block_hdr_t *cur = (block_hdr_t*)p;
        unsigned char *next_p = p + HDR_SIZE + cur->size;
        if (!cur->used && next_p + HDR_SIZE <= end) {
            block_hdr_t *next = (block_hdr_t*)next_p;
            if (!next->used) {
                cur->size += HDR_SIZE + next->size;
                continue; /* 같은 위치에서 다시 시도 */
            }
        }
        p += HDR_SIZE + cur->size;
    }
}

void *calloc(size_t nmemb, size_t size) {
    /* 곱셈 오버플로우 방지 */
    if (size != 0 && nmemb > SIZE_MAX / size) return (void*)0;
    size_t total = nmemb * size;
    void *ptr = malloc(total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}

void *realloc(void *ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) { free(ptr); return (void*)0; }

    block_hdr_t *hdr = (block_hdr_t*)((unsigned char*)ptr - HDR_SIZE);
    if (hdr->size >= size) return ptr;

    void *newptr = malloc(size);
    if (!newptr) return (void*)0;
    memcpy(newptr, ptr, hdr->size);
    free(ptr);
    return newptr;
}

/* ── 수치 변환 ────────────────────────────────────────────────────────── */
int atoi(const char *s) {
    int sign = 1, val = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') { val = val * 10 + (*s - '0'); s++; }
    return sign * val;
}

long atol(const char *s) {
    return (long)atoi(s);
}

unsigned long strtoul(const char *s, char **endptr, int base) {
    unsigned long val = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    while (1) {
        int d;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'z') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') d = *s - 'A' + 10;
        else break;
        if (d >= base) break;
        val = val * (unsigned long)base + (unsigned long)d;
        s++;
    }
    if (endptr) *endptr = (char*)s;
    return val;
}

long strtol(const char *s, char **endptr, int base) {
    int sign = 1;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    return sign * (long)strtoul(s, endptr, base);
}

int abs(int x) {
    return x < 0 ? -x : x;
}
