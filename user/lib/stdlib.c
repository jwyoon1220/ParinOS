/*
 * stdlib.c — 유저 프로그램 표준 라이브러리 구현
 *
 * malloc/free: SYS_BRK 시스템 콜을 통한 동적 힙 확장 기반
 *              (First-Fit + 인접 블록 병합)
 */

#include "stdlib.h"
#include "syscall.h"
#include "string.h"

/* ── exit ────────────────────────────────────────────────────────────── */
void exit(int code) {
    syscall1(SYS_EXIT, code);
    while (1) {}
}

/* ─────────────────────────────────────────────────────────────────────────
 * sbrk — SYS_BRK 를 통해 힙 브레이크를 delta 바이트만큼 이동시킵니다.
 *
 * 반환값: 이동 전 브레이크 주소 (실패 시 (void *)-1)
 * ───────────────────────────────────────────────────────────────────────── */
static unsigned char *g_heap_start = (unsigned char *)0; /* 힙 시작 주소 (0 = 미초기화) */
static unsigned char *g_heap_brk   = (unsigned char *)0; /* 현재 브레이크 */

static void *sbrk_impl(int delta) {
    if (g_heap_start == (unsigned char *)0) {
        /* 첫 번째 호출: 커널에서 현재 brk(=초기 힙 시작 주소)를 가져옴 */
        uint32_t init = (uint32_t)syscall1(SYS_BRK, 0);
        if ((int32_t)init <= 0) return (void *)-1;
        g_heap_start = (unsigned char *)(uintptr_t)init;
        g_heap_brk   = g_heap_start;
    }

    if (delta == 0) return (void *)g_heap_brk;

    unsigned char *old_brk = g_heap_brk;
    unsigned char *new_brk = old_brk + delta;

    /* 언더플로우 방지 */
    if (delta < 0 && new_brk > old_brk) return (void *)-1;

    uint32_t got = (uint32_t)syscall1(SYS_BRK, (int)(uint32_t)(uintptr_t)new_brk);
    if (got != (uint32_t)(uintptr_t)new_brk) return (void *)-1;

    g_heap_brk = new_brk;
    return (void *)old_brk;
}

/* ─────────────────────────────────────────────────────────────────────────
 * 동적 힙 할당기 (First-Fit + 인접 블록 병합)
 *
 * 메모리 레이아웃:
 *   [block_hdr_t | payload] [block_hdr_t | payload] ...
 *
 * block_hdr_t.size = 헤더를 제외한 페이로드 바이트 수
 * block_hdr_t.used = 1(사용 중) / 0(빈 블록)
 * ───────────────────────────────────────────────────────────────────────── */
typedef struct block_hdr {
    unsigned int size; /* 헤더 제외 페이로드 크기 */
    unsigned int used; /* 1 = 사용 중, 0 = 빈 블록 */
} block_hdr_t;

#define HDR_SIZE  ((unsigned int)sizeof(block_hdr_t))
#define MIN_SPLIT 4u  /* 분할 후 남은 페이로드의 최소 크기 */

void *malloc(size_t size) {
    if (size == 0) return (void *)0;

    /* 4바이트 정렬 */
    size = (size + 3u) & ~3u;

    /* 힙 초기화 확인 */
    if (g_heap_start == (unsigned char *)0) {
        if (sbrk_impl(0) == (void *)-1) return (void *)0;
    }

    /* ── First-Fit 탐색 ── */
    unsigned char *p   = g_heap_start;
    unsigned char *end = g_heap_brk;

    while (p + HDR_SIZE <= end) {
        block_hdr_t *hdr = (block_hdr_t *)p;
        if (!hdr->used && hdr->size >= size) {
            /* 블록 분할: 남은 공간이 충분하면 두 블록으로 쪼갬 */
            if (hdr->size >= size + HDR_SIZE + MIN_SPLIT) {
                block_hdr_t *next = (block_hdr_t *)(p + HDR_SIZE + size);
                next->size = hdr->size - size - HDR_SIZE;
                next->used = 0;
                hdr->size  = size;
            }
            hdr->used = 1;
            return (void *)(p + HDR_SIZE);
        }
        p += HDR_SIZE + hdr->size;
    }

    /* ── 힙 확장 ──
     * 요청 크기를 페이지 단위로 올림하여 syscall 빈도를 줄입니다.
     * 남는 공간은 별도 빈 블록으로 등록하여 이후 malloc 에서 재사용됩니다. */
    size_t need   = HDR_SIZE + size;
    size_t chunksz = (need + 4095u) & ~4095u; /* PAGE_SIZE=4096 으로 올림 */

    void *new_mem = sbrk_impl((int)chunksz);
    if (new_mem == (void *)-1) return (void *)0;

    block_hdr_t *hdr = (block_hdr_t *)new_mem;
    hdr->size = size;
    hdr->used = 1;

    /* 남은 공간이 헤더 + 최소 블록 이상이면 빈 블록으로 등록 */
    size_t leftover = chunksz - need;
    if (leftover >= HDR_SIZE + MIN_SPLIT) {
        block_hdr_t *extra = (block_hdr_t *)((unsigned char *)new_mem + need);
        extra->size = (unsigned int)(leftover - HDR_SIZE);
        extra->used = 0;
    }

    return (void *)((unsigned char *)new_mem + HDR_SIZE);
}

void free(void *ptr) {
    if (!ptr) return;

    block_hdr_t *hdr = (block_hdr_t *)((unsigned char *)ptr - HDR_SIZE);
    hdr->used = 0;

    /* ── 인접 빈 블록 병합 (forward coalescing) ── */
    unsigned char *p   = g_heap_start;
    unsigned char *end = g_heap_brk;

    while (p + HDR_SIZE <= end) {
        block_hdr_t *cur    = (block_hdr_t *)p;
        unsigned char *np   = p + HDR_SIZE + cur->size;

        if (!cur->used && np + HDR_SIZE <= end) {
            block_hdr_t *next = (block_hdr_t *)np;
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
    if (size != 0 && nmemb > SIZE_MAX / size) return (void *)0;
    size_t total = nmemb * size;
    void *ptr = malloc(total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}

void *realloc(void *ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) { free(ptr); return (void *)0; }

    /* 4바이트 정렬 */
    size = (size + 3u) & ~3u;

    block_hdr_t *hdr = (block_hdr_t *)((unsigned char *)ptr - HDR_SIZE);

    if (hdr->size >= size) {
        /* 블록이 더 크면 잉여 공간을 빈 블록으로 분할하여 낭비 방지 */
        if (hdr->size >= size + HDR_SIZE + MIN_SPLIT) {
            block_hdr_t *rest = (block_hdr_t *)((unsigned char *)ptr + size);
            rest->size = hdr->size - size - HDR_SIZE;
            rest->used = 0;
            hdr->size  = (unsigned int)size;
        }
        return ptr;
    }

    void *newptr = malloc(size);
    if (!newptr) return (void *)0;
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
    if (endptr) *endptr = (char *)s;
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
