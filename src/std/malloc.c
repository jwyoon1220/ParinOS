//
// Created by jwyoo on 26. 3. 8..
//

#include "../std/malloc.h"
#include "../mem/pmm.h"
#include "../mem/vmm.h"
#include "../mem/mem.h"
#include "../vga.h"


// 만약 매핑 시 Present와 RW를 동시에 주려면 (0x01 | 0x02 = 0x03)
#define PAGE_STANDARD (PAGE_PRESENT | PAGE_RW)

// 메모리 블록 헤더 구조체
typedef struct heap_block {
    size_t size;
    uint8_t is_free;
    struct heap_block* next;
    struct heap_block* prev; // 🌟 이중 연결 리스트용 포인터
} heap_block_t;

static heap_block_t* heap_start = NULL;
static uint32_t heap_end_vaddr = 0;

#define ALIGN_4(size) (((size) + 3) & ~3)

// ---------------------------------------------------------
// 힙 초기화
// ---------------------------------------------------------
void init_heap(uint32_t start_vaddr, uint32_t initial_pages) {
    heap_start = (heap_block_t*)start_vaddr;
    heap_end_vaddr = start_vaddr + (initial_pages * PAGE_SIZE);

    heap_start->size = (initial_pages * PAGE_SIZE) - sizeof(heap_block_t);
    heap_start->is_free = 1;
    heap_start->next = NULL;
    heap_start->prev = NULL; // 첫 블록의 이전은 없음

    kprintf("[malloc] Heap Initialized at %x\n", start_vaddr);
}

// ---------------------------------------------------------
// kmalloc: 동적 할당
// ---------------------------------------------------------
void* kmalloc(size_t size) {
    if (size == 0) return NULL;

    size = ALIGN_4(size);
    heap_block_t* current = heap_start;
    heap_block_t* last = NULL;

    while (current != NULL) {
        if (current->is_free && current->size >= size) {
            // 블록 쪼개기 (Splitting)
            if (current->size >= size + sizeof(heap_block_t) + 4) {
                heap_block_t* new_block = (heap_block_t*)((uint8_t*)current + sizeof(heap_block_t) + size);
                new_block->size = current->size - size - sizeof(heap_block_t);
                new_block->is_free = 1;

                // 링크 재설정 (next와 prev 모두)
                new_block->next = current->next;
                new_block->prev = current;

                if (new_block->next) {
                    new_block->next->prev = new_block;
                }

                current->size = size;
                current->next = new_block;
            }

            current->is_free = 0;
            return (void*)((uint8_t*)current + sizeof(heap_block_t));
        }
        last = current;
        current = current->next;
    }

    // 빈 공간이 없으면 확장
    void* new_phys = pmm_alloc_page();
    if (!new_phys) return NULL;

    vmm_map_page(heap_end_vaddr, (uint32_t)new_phys, PAGE_RW);

    heap_block_t* expanded_block = (heap_block_t*)heap_end_vaddr;
    expanded_block->size = PAGE_SIZE - sizeof(heap_block_t);
    expanded_block->is_free = 1;
    expanded_block->next = NULL;
    expanded_block->prev = last; // 새로 추가된 블록의 이전은 기존 마지막 블록

    if (last) last->next = expanded_block;
    heap_end_vaddr += PAGE_SIZE;

    return kmalloc(size);
}

// ---------------------------------------------------------
// kfree: 메모리 해제 및 즉시 병합
// ---------------------------------------------------------
void kfree(void* ptr) {
    if (!ptr) return;

    heap_block_t* block = (heap_block_t*)((uint8_t*)ptr - sizeof(heap_block_t));
    block->is_free = 1;

    // 1. 다음 블록이 비어있으면 합침
    if (block->next && block->next->is_free) {
        block->size += sizeof(heap_block_t) + block->next->size;
        block->next = block->next->next;
        if (block->next) block->next->prev = block;
    }

    // 2. 이전 블록이 비어있으면 합침
    if (block->prev && block->prev->is_free) {
        block->prev->size += sizeof(heap_block_t) + block->size;
        block->prev->next = block->next;
        if (block->next) block->next->prev = block->prev;
    }
}

void* kcalloc(size_t num, size_t size) {
    size_t total = num * size;
    void* ptr = kmalloc(total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}

void* krealloc(void* ptr, size_t size) {
    if (size == 0) { kfree(ptr); return NULL; }
    if (!ptr) return kmalloc(size);

    heap_block_t* block = (heap_block_t*)((uint8_t*)ptr - sizeof(heap_block_t));
    if (block->size >= size) return ptr;

    void* new_ptr = kmalloc(size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, block->size);
        kfree(ptr);
    }
    return new_ptr;
}

void dump_heap_stat() {
    heap_block_t* current = heap_start;
    size_t free_mem = 0;
    size_t used_mem = 0;
    int block_count = 0;

    while (current != NULL) {
        if (current->is_free) {
            free_mem += current->size;
        } else {
            used_mem += current->size;
        }
        block_count++;
        current = current->next;
    }

    kprintf("--- Memory Usage ---\n");
    kprintf("Total Heap: %d KB\n", (free_mem + used_mem) / 1024);
    kprintf("Used: %d KB (%d bytes)\n", used_mem / 1024, used_mem);
    kprintf("Free: %d KB (%d bytes)\n", free_mem / 1024, free_mem);
    kprintf("Total Blocks: %d\n", block_count);
    kprintf("--------------------\n");
}