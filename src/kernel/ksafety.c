//
// src/kernel/ksafety.c — 커널 안전성 유틸리티 구현
//

#include "ksafety.h"
#include "../hal/vga.h"
#include "../mem/mem.h"

void ksafety_validate_ptr(const void *ptr, size_t size, int in_kernel) {
    uint32_t addr = (uint32_t)ptr;
    if (ptr == NULL) {
        kernel_panic("NULL pointer dereference", 0);
    }
    if (size == 0) return;

    /* 주소 + 크기 넘침 검사 */
    if (addr + (uint32_t)size < addr) {
        kernel_panic("Pointer arithmetic overflow", addr);
    }

    if (in_kernel) {
        if (!ksafety_is_kernel_range(addr)) {
            klog_warn("[ksafety] Suspicious kernel ptr 0x%x (size %u)\n",
                      addr, (unsigned)size);
        }
    } else {
        if (!ksafety_is_user_range(addr)) {
            klog_warn("[ksafety] Suspicious user ptr 0x%x (size %u)\n",
                      addr, (unsigned)size);
        }
    }
}

void *ksafe_memcpy(void *dest, const void *src, size_t n) {
    if (!dest || !src || n == 0) return NULL;
    return memcpy(dest, src, n);
}

void *ksafe_memset(void *dest, int val, size_t n) {
    if (!dest || n == 0) return NULL;
    return memset(dest, val, n);
}

char *ksafe_strncpy(char *dest, const char *src, size_t n) {
    if (!dest || !src || n == 0) return dest;
    size_t i = 0;
    for (; i < n - 1 && src[i]; i++) dest[i] = src[i];
    dest[i] = '\0';
    return dest;
}
