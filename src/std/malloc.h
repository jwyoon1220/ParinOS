//
// Created by jwyoo on 26. 3. 8..
//

#ifndef PARINOS_MALLOC_H
#define PARINOS_MALLOC_H

#include <stdint.h>
#include <stddef.h>

// 힙 영역 초기화 (kmain에서 한 번 호출)
void init_heap(uint32_t start_vaddr, uint32_t initial_pages);

// 표준 동적 할당 함수들
void* kmalloc(size_t size);
void* kcalloc(size_t num, size_t size);
void  kfree(void* ptr);
void* krealloc(void* ptr, size_t size);
void dump_heap_stat();
// 디버깅용 힙 상태 출력
void dump_heap();

#endif //PARINOS_MALLOC_H