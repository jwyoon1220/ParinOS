#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include <stddef.h>

#define PAGE_SIZE 4096
#define FRAME_SIZE PAGE_SIZE  // Frame과 Page는 동일한 크기
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

// Page 할당/해제 (가상 메모리 관점)
void* pmm_alloc_page();
void pmm_free_page(void* ptr);
void* pmm_alloc_pages(uint32_t count);
void pmm_free_pages(void* ptr, uint32_t count);

// Frame 할당/해제 (물리 메모리 관점) - AHCI 등 드라이버용
void* pmm_alloc_frame();
void pmm_free_frame(void* ptr);
void* pmm_alloc_frames(uint32_t count);
void pmm_free_frames(void* ptr, uint32_t count);

// 메모리 상태 조회
uint32_t pmm_get_free_memory();
uint32_t pmm_get_used_memory();
uint32_t pmm_get_total_memory();

// 디버깅 함수
void dump_memory(uint32_t start_addr, int lines);
void pmm_dump_stats();

#endif