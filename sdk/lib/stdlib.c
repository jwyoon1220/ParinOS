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

/* ── qsort ───────────────────────────────────────────────────────────────── */

static void _swap(unsigned char *a, unsigned char *b, size_t sz) {
    while (sz--) { unsigned char t = *a; *a++ = *b; *b++ = t; }
}

static void _qsort_r(unsigned char *base, size_t lo, size_t hi, size_t sz,
                     int (*cmp)(const void *, const void *)) {
    while (lo < hi) {
        /* Insertion sort for small partitions */
        if (hi - lo < 8) {
            for (size_t i = lo + 1; i <= hi; i++) {
                size_t j = i;
                while (j > lo && cmp(base+(j-1)*sz, base+j*sz) > 0) {
                    _swap(base+(j-1)*sz, base+j*sz, sz);
                    j--;
                }
            }
            return;
        }
        /* Median-of-three pivot */
        size_t mid = lo + (hi - lo) / 2;
        if (cmp(base+lo*sz, base+mid*sz) > 0) _swap(base+lo*sz, base+mid*sz, sz);
        if (cmp(base+lo*sz, base+hi*sz)  > 0) _swap(base+lo*sz, base+hi*sz,  sz);
        if (cmp(base+mid*sz, base+hi*sz) > 0) _swap(base+mid*sz, base+hi*sz, sz);
        /* Place pivot at hi-1 */
        _swap(base+mid*sz, base+(hi-1)*sz, sz);
        unsigned char *pivot = base + (hi-1)*sz;

        size_t i = lo, j = hi - 1;
        for (;;) {
            while (cmp(base + (++i)*sz, pivot) < 0) {}
            while (j > lo && cmp(base + (--j)*sz, pivot) > 0) {}
            if (i >= j) break;
            _swap(base+i*sz, base+j*sz, sz);
        }
        _swap(base+i*sz, base+(hi-1)*sz, sz); /* restore pivot */

        /* Recurse on smaller partition, iterate on larger (limit stack) */
        if (i - 1 - lo < hi - i - 1) {
            if (i > lo + 1) _qsort_r(base, lo, i - 1, sz, cmp);
            lo = i + 1;
        } else {
            if (i + 1 < hi) _qsort_r(base, i + 1, hi, sz, cmp);
            if (i == 0) break;
            hi = i - 1;
        }
    }
}

void qsort(void *base, size_t nmemb, size_t sz,
           int (*cmp)(const void *, const void *)) {
    if (!base || nmemb < 2 || sz == 0) return;
    _qsort_r((unsigned char*)base, 0, nmemb - 1, sz, cmp);
}

/* ── bsearch ─────────────────────────────────────────────────────────────── */

void *bsearch(const void *key, const void *base, size_t nmemb, size_t sz,
              int (*cmp)(const void *, const void *)) {
    const unsigned char *p = (const unsigned char*)base;
    size_t lo = 0, hi = nmemb;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int r = cmp(key, p + mid * sz);
        if      (r == 0) return (void*)(p + mid * sz);
        else if (r  < 0) hi = mid;
        else             lo = mid + 1;
    }
    return (void*)0;
}
