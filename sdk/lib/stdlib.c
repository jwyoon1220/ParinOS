/*
 * sdk/lib/stdlib.c — ParinOS SDK: Standard Library Implementation
 *
 * malloc/free: static heap pool with first-fit algorithm (64 KB).
 */

#include "stdlib.h"
#include "syscall.h"
#include "string.h"

/* ── exit ────────────────────────────────────────────────────────────────── */
void exit(int code) {
    syscall1(SYS_EXIT, code);
    while (1) {}
}

/* ── Static heap (64 KB) ─────────────────────────────────────────────────── */
#define HEAP_SIZE (64 * 1024)

static unsigned char g_heap[HEAP_SIZE];
static int           g_heap_init = 0;

/* Block header: payload size (uint32) + in-use flag (uint32) */
typedef struct block_hdr {
    unsigned int size;  /* Payload size excluding header */
    unsigned int used;  /* 1 = allocated, 0 = free */
} block_hdr_t;

#define HDR_SIZE sizeof(block_hdr_t)

static void heap_init(void) {
    block_hdr_t *hdr = (block_hdr_t*)g_heap;
    hdr->size = HEAP_SIZE - HDR_SIZE;
    hdr->used = 0;
    g_heap_init = 1;
}

void *malloc(size_t size) {
    if (!g_heap_init) heap_init();
    if (size == 0) return (void*)0;

    /* 4-byte alignment */
    size = (size + 3) & ~3u;

    unsigned char *p   = g_heap;
    unsigned char *end = g_heap + HEAP_SIZE;

    while (p + HDR_SIZE <= end) {
        block_hdr_t *hdr = (block_hdr_t*)p;
        if (!hdr->used && hdr->size >= size) {
            /* Split block if remainder fits a header + 4 bytes */
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
    return (void*)0;
}

void free(void *ptr) {
    if (!ptr) return;
    block_hdr_t *hdr = (block_hdr_t*)((unsigned char*)ptr - HDR_SIZE);
    hdr->used = 0;

    /* Coalesce adjacent free blocks */
    unsigned char *p   = g_heap;
    unsigned char *end = g_heap + HEAP_SIZE;
    while (p + HDR_SIZE <= end) {
        block_hdr_t *cur    = (block_hdr_t*)p;
        unsigned char *next_p = p + HDR_SIZE + cur->size;
        if (!cur->used && next_p + HDR_SIZE <= end) {
            block_hdr_t *next = (block_hdr_t*)next_p;
            if (!next->used) {
                cur->size += HDR_SIZE + next->size;
                continue;
            }
        }
        p += HDR_SIZE + cur->size;
    }
}

void *calloc(size_t nmemb, size_t size) {
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

/* ── Numeric conversion ──────────────────────────────────────────────────── */
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
