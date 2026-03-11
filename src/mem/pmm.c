//
// Created by jwyoo on 26. 3. 7..
//
#include "pmm.h"
#include "../vga.h" // kprintf 사용

#define MMAP_COUNT_ADDR 0x8000
#define MMAP_DATA_ADDR  0x8004

static uint8_t pmm_bitmap[BITMAP_SIZE];
static uint32_t total_pages = 0;
static uint32_t used_pages = 0;

inline int bitmap_test(uint32_t page_idx) {
    if (page_idx >= total_pages) return 1; // 범위를 벗어나면 사용 중으로 간주
    return pmm_bitmap[page_idx / 8] & (1 << (page_idx % 8));
}

// 비트 제어 도우미 함수들
static void bitmap_set(uint32_t page_idx) {
    if (page_idx >= total_pages) return;
    if (!bitmap_test(page_idx)) {
        pmm_bitmap[page_idx / 8] |= (1 << (page_idx % 8));
        used_pages++;
    }
}

static void bitmap_unset(uint32_t page_idx) {
    if (page_idx >= total_pages) return;
    if (bitmap_test(page_idx)) {
        pmm_bitmap[page_idx / 8] &= ~(1 << (page_idx % 8));
        used_pages--;
    }
}

void print_total_memory() {
    // 0x8000에서 엔트리 개수를 가져옴
    uint16_t entry_count = *(uint16_t*)0x8000;
    // 0x8004에서 첫 번째 엔트리 주소를 가져옴
    mmap_entry_t* mmap = (mmap_entry_t*)0x8004;

    uint32_t total_mb = 0;

    for (int i = 0; i < entry_count; i++) {
        // Type 1 (Usable)인 영역만 더함
        if (mmap[i].type == 1) {
            total_mb += (uint32_t)(mmap[i].length / 1024 / 1024);
        }
    }

    kprintf("%d MB OK.\n", total_mb);
}

void init_pmm() {
    uint16_t entry_count = *(uint16_t*)MMAP_COUNT_ADDR;
    mmap_entry_t* mmap = (mmap_entry_t*)MMAP_DATA_ADDR;

    uint64_t max_addr = 0;
    uint32_t usable_mem = 0;
    used_pages = 0;

    // 1. 비트맵을 일단 모두 '사용 중(1)'으로 채움 (보수적 초기화)
    for (uint32_t i = 0; i < BITMAP_SIZE; i++) {
        pmm_bitmap[i] = 0xFF;
    }

    // 2. 메모리 맵을 돌면서 가용 메모리(Type 1) 파악 및 비트맵 해제
    for (int i = 0; i < entry_count; i++) {
        if (mmap[i].type == 1) { // Usable RAM
            uint64_t start_addr = mmap[i].base;
            uint64_t end_addr = start_addr + mmap[i].length;

            if (end_addr > max_addr) max_addr = end_addr;
            usable_mem += (uint32_t)(mmap[i].length / 1024 / 1024);

            // 해당 영역의 비트맵을 '비어있음(0)'으로 설정
            // 단, 1MB 이하(BIOS/VGA)는 안전을 위해 건너뜀
            uint32_t start_page = (uint32_t)(start_addr / PAGE_SIZE);
            uint32_t end_page = (uint32_t)(end_addr / PAGE_SIZE);

            for (uint32_t p = start_page; p < end_page; p++) {
                if (p >= (1024 * 1024 / PAGE_SIZE)) { // 1MB 이상만 해제
                    pmm_bitmap[p / 8] &= ~(1 << (p % 8)); // used_pages 증가 없이 직접 해제
                }
            }
        }
    }

    // 시스템 전체 페이지 수 설정
    total_pages = (uint32_t)(max_addr / PAGE_SIZE);

    // 사용 중인 페이지 수 계산
    used_pages = 0;
    for (uint32_t i = 0; i < total_pages; i++) {
        if (bitmap_test(i)) used_pages++;
    }

    // 3. 커널 영역 보호 (_kernel_start ~ _kernel_end)
    uint32_t k_start = (uint32_t)&_kernel_start;
    uint32_t k_end   = (uint32_t)&_kernel_end;
    uint32_t k_start_p = k_start / PAGE_SIZE;
    uint32_t k_end_p = (k_end + PAGE_SIZE - 1) / PAGE_SIZE;

    for (uint32_t i = k_start_p; i < k_end_p; i++) {
        bitmap_set(i);
    }

    // 로깅
    kprintf("[PMM] Initiated with %d MB Usable RAM.\n", usable_mem);
    kprintf("[PMM] Max Address: %x, Total Pages: %d\n", (uint32_t)max_addr, total_pages);
}

// =============================================================================
// PAGE 할당/해제 함수들 (가상 메모리 관점)
// =============================================================================

void* pmm_alloc_page() {
    for (uint32_t i = 0; i < total_pages; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            return (void*)(i * PAGE_SIZE);
        }
    }
    kprintf("[PMM] Out of Memory! (alloc_page)\n");
    return NULL;
}

void pmm_free_page(void* ptr) {
    if (ptr == NULL) return;
    uint32_t page_idx = (uint32_t)ptr / PAGE_SIZE;
    bitmap_unset(page_idx);
}

// 연속된 n개의 빈 페이지를 찾아 할당
void* pmm_alloc_pages(uint32_t count) {
    if (count == 0) return NULL;

    uint32_t continuous = 0;
    uint32_t start_page = 0;

    for (uint32_t i = 0; i < total_pages; i++) {
        if (!bitmap_test(i)) {
            if (continuous == 0) start_page = i;
            continuous++;
            if (continuous == count) {
                for (uint32_t j = start_page; j < start_page + count; j++) {
                    bitmap_set(j);
                }
                return (void*)(start_page * PAGE_SIZE);
            }
        } else {
            continuous = 0;
        }
    }
    kprintf("[PMM] Out of Memory! (alloc_pages, count=%d)\n", count);
    return NULL; // 할당 실패
}

// 여러 페이지 해제
void pmm_free_pages(void* ptr, uint32_t count) {
    if (ptr == NULL || count == 0) return;

    uint32_t start_page = (uint32_t)ptr / PAGE_SIZE;
    for (uint32_t i = start_page; i < start_page + count; i++) {
        bitmap_unset(i);
    }
}

// =============================================================================
// FRAME 할당/해제 함수들 (물리 메모리 관점) - 드라이버용
// =============================================================================

void* pmm_alloc_frame() {
    return pmm_alloc_page(); // Frame과 Page는 동일한 구현
}

void pmm_free_frame(void* ptr) {
    pmm_free_page(ptr); // Frame과 Page는 동일한 구현
}

void* pmm_alloc_frames(uint32_t count) {
    return pmm_alloc_pages(count); // Frame과 Page는 동일한 구현
}

void pmm_free_frames(void* ptr, uint32_t count) {
    pmm_free_pages(ptr, count); // Frame과 Page는 동일한 구현
}

// =============================================================================
// 메모리 상태 조회 함수들
// =============================================================================

uint32_t pmm_get_free_memory() {
    return (total_pages - used_pages) * PAGE_SIZE;
}

uint32_t pmm_get_used_memory() {
    return used_pages * PAGE_SIZE;
}

uint32_t pmm_get_total_memory() {
    return total_pages * PAGE_SIZE;
}

// PMM 상태 출력
void pmm_dump_stats() {
    kprintf("\n=== PMM Statistics ===\n");
    kprintf("Total Pages:   %d (%d KB)\n", total_pages, (total_pages * PAGE_SIZE) / 1024);
    kprintf("Used Pages:    %d (%d KB)\n", used_pages, (used_pages * PAGE_SIZE) / 1024);
    kprintf("Free Pages:    %d (%d KB)\n", total_pages - used_pages, ((total_pages - used_pages) * PAGE_SIZE) / 1024);
    kprintf("Page Size:     %d bytes\n", PAGE_SIZE);
    kprintf("Memory Usage:  %d%%\n", (used_pages * 100) / total_pages);
}

// 메모리 덤프 함수 (기존과 동일)
void dump_memory(uint32_t start_addr, int lines) {
    uint8_t* ptr = (uint8_t*)start_addr;

    // 헤더 정렬 (Address 10칸, 데이터 24칸)
    kprintf("\n Address    | 0000 0001 0002 0003 0004 0005 0006 0007 | Value\n");
    kprintf("------------+----------------------------------------+----------\n");

    for (int i = 0; i < lines; i++) {
        uint32_t addr = (uint32_t)ptr;

        // 1. 주소 출력부 (0x 포함 10자리 고정)
        if (addr < 0x10000000) kprint("0");
        if (addr < 0x1000000)  kprint("0");
        if (addr < 0x100000)   kprint("0");
        if (addr < 0x10000)    kprint("0");
        if (addr < 0x1000)     kprint("0");
        if (addr < 0x100)      kprint("0");
        if (addr < 0x10)       kprint("0");
        kprintf("%x", addr); // 여기서 0x가 안 붙게 kprintf가 숫자만 출력해야 함

        kprint(" | ");

        // 2. 데이터 출력부 (바이트당 3칸 고정: "XX ")
        for (int j = 0; j < 8; j++) {
            uint8_t val = ptr[j];
            if (val < 0x10) kprint("0");
            kprintf("%x", val); // 숫자만 출력
            kprint(" ");        // 공백 직접 제어
        }

        kprint("| ");

        // 3. ASCII Value 출력부
        for (int j = 0; j < 8; j++) {
            char c = (char)ptr[j];
            if (c >= 32 && c <= 126) {
                lkputchar(c);
            } else {
                lkputchar('.');
            }
        }

        kprint("\n");
        ptr += 8;
    }
}