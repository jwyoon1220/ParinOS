//
// Created by jwyoo on 26. 3. 8..
//

#include "../std/malloc.h"
#include "../mem/pmm.h"
#include "../mem/vmm.h"
#include "../mem/mem.h"
#include "../vga.h"

// 메모리 블록 헤더 구조체 (데이터 앞에 항상 붙어있음)
typedef struct heap_block {
    size_t size;             // 블록의 크기 (헤더 크기 제외)
    uint8_t is_free;         // 1이면 사용 가능, 0이면 사용 중
    struct heap_block* next; // 다음 블록을 가리키는 포인터
} heap_block_t;

static heap_block_t* heap_start = NULL;
static uint32_t heap_end_vaddr = 0; // 현재 힙이 매핑된 끝 가상 주소

// 4바이트 정렬 매크로 (메모리 접근 속도 최적화)
#define ALIGN_4(size) (((size) + 3) & ~3)

// ---------------------------------------------------------
// 힙 초기화
// ---------------------------------------------------------
// malloc.c 수정 제안
void init_heap(uint32_t start_vaddr, uint32_t initial_pages) {
    heap_start = (heap_block_t*)start_vaddr;

    // 🌟 이미 vmm.c에서 0~16MB를 매핑했다면
    // 여기서 vmm_map_page를 또 호출할 필요가 없습니다! (중복 매핑 방지)
    // 만약 16MB 밖을 쓰고 싶다면 여기서 매핑하되, vmm_map_page가 가상 주소를 쓰도록 고쳐야 합니다.

    heap_end_vaddr = start_vaddr + (initial_pages * PAGE_SIZE);

    // 첫 블록 설정
    heap_start->size = (initial_pages * PAGE_SIZE) - sizeof(heap_block_t);
    heap_start->is_free = 1;
    heap_start->next = NULL;

    kprintf("[malloc] Heap Initialized at %x\n", start_vaddr);
}

// ---------------------------------------------------------
// kmalloc: 동적 할당
// ---------------------------------------------------------
void* kmalloc(size_t size) {
    if (size == 0) return NULL;

    size = ALIGN_4(size); // 4바이트 배수로 크기 맞춤
    heap_block_t* current = heap_start;
    heap_block_t* last = NULL;

    // 1. First-Fit: 앞에서부터 빈 공간 찾기
    while (current != NULL) {
        if (current->is_free && current->size >= size) {
            // 빈 공간을 찾았는데, 크기가 너무 크면 쪼개기 (Splitting)
            if (current->size >= size + sizeof(heap_block_t) + 4) {
                heap_block_t* new_block = (heap_block_t*)((uint8_t*)current + sizeof(heap_block_t) + size);
                new_block->size = current->size - size - sizeof(heap_block_t);
                new_block->is_free = 1;
                new_block->next = current->next;

                current->size = size;
                current->next = new_block;
            }

            current->is_free = 0;
            // 헤더 바로 다음 주소를 반환
            return (void*)((uint8_t*)current + sizeof(heap_block_t));
        }
        last = current;
        current = current->next;
    }

    // 2. 빈 공간이 없으면 힙 영역 확장 (Heap Expansion)
    // PMM에서 새 물리 페이지를 받아와서 현재 힙 끝(가상 주소)에 매핑
    void* new_phys = pmm_alloc_page();
    if (!new_phys) return NULL; // 진짜 메모리 부족

    vmm_map_page(heap_end_vaddr, (uint32_t)new_phys, PAGE_RW);

    // 새 블록 생성
    heap_block_t* expanded_block = (heap_block_t*)heap_end_vaddr;
    expanded_block->size = PAGE_SIZE - sizeof(heap_block_t);
    expanded_block->is_free = 1;
    expanded_block->next = NULL;

    if (last) last->next = expanded_block;
    heap_end_vaddr += PAGE_SIZE;

    // 확장했으니 다시 malloc 호출 (재귀)
    return kmalloc(size);
}

// ---------------------------------------------------------
// kfree: 메모리 해제 및 병합
// ---------------------------------------------------------
void kfree(void* ptr) {
    if (!ptr) return;

    // 데이터 포인터에서 헤더 크기만큼 앞으로 가면 관리 블록이 나옴
    heap_block_t* block = (heap_block_t*)((uint8_t*)ptr - sizeof(heap_block_t));
    block->is_free = 1;

    // 병합 (Coalescing): 파편화(Fragmentation) 방지
    // 현재 블록과 다음 블록이 모두 비어있으면 하나로 합침
    heap_block_t* current = heap_start;
    while (current != NULL) {
        if (current->is_free && current->next != NULL && current->next->is_free) {
            current->size += sizeof(heap_block_t) + current->next->size;
            current->next = current->next->next;
        }
        current = current->next;
    }
}

// ---------------------------------------------------------
// kcalloc: 할당 후 0으로 초기화
// ---------------------------------------------------------
void* kcalloc(size_t num, size_t size) {
    size_t total = num * size;
    void* ptr = kmalloc(total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

// ---------------------------------------------------------
// krealloc: 크기 재조정
// ---------------------------------------------------------
void* krealloc(void* ptr, size_t size) {
    if (size == 0) {
        kfree(ptr);
        return NULL;
    }
    if (!ptr) return kmalloc(size);

    heap_block_t* block = (heap_block_t*)((uint8_t*)ptr - sizeof(heap_block_t));
    if (block->size >= size) {
        return ptr; // 이미 충분히 크면 그대로 반환
    }

    // 새 공간 할당, 기존 데이터 복사, 기존 공간 해제
    void* new_ptr = kmalloc(size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, block->size);
        kfree(ptr);
    }
    return new_ptr;
}