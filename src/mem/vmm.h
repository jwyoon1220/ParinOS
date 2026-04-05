//
// Created by jwyoo on 26. 3. 8..
//

#ifndef PARINOS_VMM_H
#define PARINOS_VMM_H

#include <stdint.h>
#include <stddef.h>

// 페이지 크기 및 마스크
#define PAGE_SIZE           4096
#define PAGE_MASK           0xFFFFF000
#define PAGE_OFFSET_MASK    0x00000FFF

// 페이지 디렉토리/테이블 인덱스 추출
#define PD_INDEX(vaddr)     ((vaddr) >> 22)
#define PT_INDEX(vaddr)     (((vaddr) >> 12) & 0x3FF)

// 페이지 플래그
#define PAGE_PRESENT        0x001
#define PAGE_WRITE          0x002
#define PAGE_USER           0x004
#define PAGE_WRITETHROUGH   0x008
#define PAGE_CACHEDISABLE   0x010
#define PAGE_ACCESSED       0x020
#define PAGE_DIRTY          0x040
#define PAGE_SIZE_4MB       0x080
#define PAGE_GLOBAL         0x100

// 기본 플래그 조합
#define PAGE_FLAGS_KERNEL   (PAGE_PRESENT | PAGE_WRITE)
// vmm.h
#define PAGE_RW       0x02   // Bit 1 (Read/Write)
#define PAGE_FLAGS_USER     (PAGE_PRESENT | PAGE_WRITE | PAGE_USER)

// 가상 메모리 영역 정의
#define KERNEL_VIRTUAL_BASE 0xC0000000  // 3GB (커널 공간)
#define KERNEL_HEAP_BASE    0x800000    // 8MB (커널 힙)
#define USER_SPACE_BASE     0x00400000  // 4MB (유저 공간)
#define IDENTITY_MAP_SIZE   0x1000000   // 16MB (Identity mapping)

// VMM 상태
typedef enum {
    VMM_SUCCESS = 0,
    VMM_ERROR_NO_MEMORY,
    VMM_ERROR_ALREADY_MAPPED,
    VMM_ERROR_NOT_MAPPED,
    VMM_ERROR_INVALID_ADDRESS,
    VMM_ERROR_PERMISSION_DENIED
} vmm_result_t;

// 페이지 할당자 타입
typedef enum {
    VMM_ALLOC_KERNEL,   // 커널용 (물리 메모리 직접 할당)
    VMM_ALLOC_USER,     // 유저용 (요청 시 할당)
    VMM_ALLOC_DEVICE    // 디바이스용 (특정 물리 주소 매핑)
} vmm_alloc_type_t;

// VMM 통계
typedef struct {
    uint32_t total_virtual_pages;
    uint32_t mapped_pages;
    uint32_t kernel_pages;
    uint32_t user_pages;
    uint32_t page_tables_count;
    uint32_t page_fault_count;
} vmm_stats_t;

// 함수 프로토타입
void init_vmm(void);
vmm_result_t vmm_map_page(uint32_t vaddr, uint32_t paddr, uint32_t flags);
vmm_result_t vmm_map_pages(uint32_t vaddr, uint32_t paddr, uint32_t count, uint32_t flags);
vmm_result_t vmm_unmap_page(uint32_t vaddr);
vmm_result_t vmm_unmap_pages(uint32_t vaddr, uint32_t count);
void* vmm_get_physical_address(uint32_t vaddr);
vmm_result_t vmm_alloc_virtual_pages(uint32_t vaddr, uint32_t count, vmm_alloc_type_t type);
uint32_t vmm_find_free_virtual_address(uint32_t start, uint32_t end, uint32_t count);

// TLB 관리
void vmm_invalidate_page(uint32_t vaddr);
void vmm_invalidate_range(uint32_t start, uint32_t end);
void vmm_flush_tlb(void);

// 페이지 폴트 처리
void page_fault_handler(uint32_t error_code);

// 디버깅 및 통계
void vmm_dump_page_directory(void);
void vmm_dump_page_table(uint32_t pd_index);
vmm_stats_t vmm_get_stats(void);
void vmm_print_stats(void);

// 주소 유효성 검사
int vmm_is_mapped(uint32_t vaddr);
int vmm_is_kernel_address(uint32_t vaddr);
int vmm_is_user_address(uint32_t vaddr);

// 프로세스별 페이지 디렉토리 지원
/**
 * 커널 매핑을 공유하는 새 페이지 디렉토리를 생성합니다.
 * 유저 영역(PD 엔트리 1: 0x400000-0x7FFFFF)은 비워 프로세스 격리를 구현합니다.
 * @return 새 페이지 디렉토리의 물리 주소 (실패 시 0)
 */
uint32_t vmm_clone_kernel_dir(void);

/**
 * CR3를 새 페이지 디렉토리로 전환합니다.
 * @param phys_pd  새 페이지 디렉토리의 물리 주소
 */
void vmm_switch_page_dir(uint32_t phys_pd);

/**
 * 부트 페이지 디렉토리의 물리 주소를 반환합니다.
 */
uint32_t vmm_get_boot_dir_phys(void);

extern uint32_t* current_page_directory;

#endif //PARINOS_VMM_H