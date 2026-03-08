#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include <stddef.h>

#define PAGE_SIZE 4096
#define RAM_MAX_SIZE (128 * 1024 * 1024)
#define BITMAP_SIZE  (RAM_MAX_SIZE / PAGE_SIZE / 8)

extern uint32_t _kernel_start;
extern uint32_t _kernel_end;

// 1. 메모리 맵 엔트리 구조체 정의
typedef struct {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t acpi;
} __attribute__((packed)) mmap_entry_t;

void init_pmm();
void* pmm_alloc_page();
void pmm_free_page(void* ptr);

// 🌟 이 부분을 수정하세요 (count 추가)
void* pmm_alloc_pages(uint32_t count);
void pmm_free_pages(void* ptr, uint32_t count);

void dump_memory(uint32_t start_addr, int lines);

#endif