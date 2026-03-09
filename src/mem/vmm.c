//
// Created by jwyoo on 26. 3. 8..
//

#include "vmm.h"
#include "pmm.h"
#include "mem.h"
#include "../vga.h"
#include "../std/string.h"
#include "../kernel/kernel_status_manager.h"

// 전역 변수
uint32_t* current_page_directory = NULL;
static vmm_stats_t vmm_statistics = {0};

// 부트 페이지 디렉토리와 페이지 테이블 (16MB Identity mapping)
static uint32_t boot_page_directory[1024] __attribute__((aligned(4096)));
static uint32_t boot_page_tables[4][1024] __attribute__((aligned(4096)));

// 임시 매핑을 위한 가상 주소 슬롯들
#define TEMP_MAP_BASE           0x00FF0000  // 15.9MB ~ 16MB (64KB)
#define TEMP_MAP_SIZE           0x10000     // 64KB
#define TEMP_MAP_SLOTS          16          // 16개 슬롯 (각 4KB)
#define TEMP_MAP_SLOT_SIZE      PAGE_SIZE

static uint32_t temp_map_slots[TEMP_MAP_SLOTS];
static int temp_map_used[TEMP_MAP_SLOTS] = {0};

// 외부 링커 심볼
extern uint32_t _kernel_start;
extern uint32_t _kernel_end;

// =============================================================================
// 임시 매핑 관리 함수들
// =============================================================================

// 사용 가능한 임시 매핑 슬롯 찾기
static int vmm_find_free_temp_slot(void) {
    for (int i = 0; i < TEMP_MAP_SLOTS; i++) {
        if (!temp_map_used[i]) {
            return i;
        }
    }
    return -1; // 모든 슬롯이 사용 중
}

// 물리 주소를 임시로 가상 주소에 매핑
static void* vmm_temp_map(uint32_t paddr) {
    int slot = vmm_find_free_temp_slot();
    if (slot == -1) {
        kprintf("[VMM] All temporary mapping slots are in use!\n");
        return NULL;
    }

    uint32_t vaddr = TEMP_MAP_BASE + slot * TEMP_MAP_SLOT_SIZE;
    uint32_t pd_idx = PD_INDEX(vaddr);
    uint32_t pt_idx = PT_INDEX(vaddr);

    // 임시 매핑은 항상 Identity mapping 범위 내의 페이지 테이블을 사용
    if (!(current_page_directory[pd_idx] & PAGE_PRESENT)) {
        kprintf("[VMM] Temporary mapping page table not present! This should not happen.\n");
        return NULL;
    }

    uint32_t* page_table = (uint32_t*)(current_page_directory[pd_idx] & PAGE_MASK);

    // 기존 매핑이 있다면 오류
    if (page_table[pt_idx] & PAGE_PRESENT) {
        kprintf("[VMM] Temporary slot %d already mapped! vaddr=%x\n", slot, vaddr);
        return NULL;
    }

    // 임시 매핑 설정
    page_table[pt_idx] = (paddr & PAGE_MASK) | PAGE_FLAGS_KERNEL;
    temp_map_slots[slot] = paddr & PAGE_MASK;
    temp_map_used[slot] = 1;

    // TLB 무효화
    vmm_invalidate_page(vaddr);

    return (void*)vaddr;
}

// 임시 매핑 해제
static void vmm_temp_unmap(void* temp_vaddr) {
    uint32_t vaddr = (uint32_t)temp_vaddr;

    // 임시 매핑 범위 확인
    if (vaddr < TEMP_MAP_BASE || vaddr >= TEMP_MAP_BASE + TEMP_MAP_SIZE) {
        kprintf("[VMM] Invalid temporary mapping address: %x\n", vaddr);
        return;
    }

    int slot = (vaddr - TEMP_MAP_BASE) / TEMP_MAP_SLOT_SIZE;
    if (slot >= TEMP_MAP_SLOTS || !temp_map_used[slot]) {
        kprintf("[VMM] Invalid temporary mapping slot: %d\n", slot);
        return;
    }

    uint32_t pd_idx = PD_INDEX(vaddr);
    uint32_t pt_idx = PT_INDEX(vaddr);

    uint32_t* page_table = (uint32_t*)(current_page_directory[pd_idx] & PAGE_MASK);
    page_table[pt_idx] = 0;

    temp_map_slots[slot] = 0;
    temp_map_used[slot] = 0;

    // TLB 무효화
    vmm_invalidate_page(vaddr);
}

// 물리 주소에 안전하게 접근하는 함수
static void* vmm_access_physical(uint32_t paddr, uint32_t* temp_mapped) {
    *temp_mapped = 0;

    // Identity mapping 범위 내라면 직접 접근
    if (paddr < IDENTITY_MAP_SIZE) {
        return (void*)paddr;
    }

    // Identity mapping 범위 밖이라면 임시 매핑 사용
    void* temp_vaddr = vmm_temp_map(paddr);
    if (temp_vaddr) {
        *temp_mapped = 1;
        return temp_vaddr;
    }

    return NULL;
}

// 임시 매핑으로 접근한 메모리를 해제
static void vmm_release_physical(void* addr, uint32_t was_temp_mapped) {
    if (was_temp_mapped) {
        vmm_temp_unmap(addr);
    }
}

// =============================================================================
// TLB 관리 함수들 (기존과 동일)
// =============================================================================

void vmm_invalidate_page(uint32_t vaddr) {
    __asm__ __volatile__("invlpg (%0)" : : "r"(vaddr) : "memory");
}

void vmm_invalidate_range(uint32_t start, uint32_t end) {
    start = start & PAGE_MASK;
    end = (end + PAGE_SIZE - 1) & PAGE_MASK;

    for (uint32_t addr = start; addr < end; addr += PAGE_SIZE) {
        vmm_invalidate_page(addr);
    }
}

void vmm_flush_tlb(void) {
    uint32_t reg;
    __asm__ __volatile__ (
        "mov %%cr3, %0\n\t"
        "mov %0, %%cr3"
        : "=r"(reg)
        :
        : "memory"
    );
}

// =============================================================================
// 주소 유효성 검사 (기존과 동일)
// =============================================================================

int vmm_is_kernel_address(uint32_t vaddr) {
    return vaddr >= KERNEL_VIRTUAL_BASE || vaddr < IDENTITY_MAP_SIZE;
}

int vmm_is_user_address(uint32_t vaddr) {
    return vaddr >= USER_SPACE_BASE && vaddr < KERNEL_VIRTUAL_BASE;
}

int vmm_is_mapped(uint32_t vaddr) {
    uint32_t pd_idx = PD_INDEX(vaddr);
    uint32_t pt_idx = PT_INDEX(vaddr);

    if (!current_page_directory) return 0;
    if (!(current_page_directory[pd_idx] & PAGE_PRESENT)) return 0;

    uint32_t pt_paddr = current_page_directory[pd_idx] & PAGE_MASK;
    uint32_t temp_mapped;
    uint32_t* page_table = (uint32_t*)vmm_access_physical(pt_paddr, &temp_mapped);

    if (!page_table) return 0;

    int result = (page_table[pt_idx] & PAGE_PRESENT) != 0;

    vmm_release_physical(page_table, temp_mapped);
    return result;
}

// =============================================================================
// 개선된 페이지 테이블 할당
// =============================================================================

static uint32_t* vmm_alloc_page_table(void) {
    void* pt_phys = pmm_alloc_frame();
    if (!pt_phys) {
        kprintf("[VMM] Failed to allocate page table\n");
        return NULL;
    }

    uint32_t pt_paddr = (uint32_t)pt_phys;
    uint32_t temp_mapped;
    uint32_t* pt_vaddr = (uint32_t*)vmm_access_physical(pt_paddr, &temp_mapped);

    if (!pt_vaddr) {
        kprintf("[VMM] Failed to map page table for initialization\n");
        pmm_free_frame(pt_phys);
        return NULL;
    }

    // 페이지 테이블 초기화
    memset(pt_vaddr, 0, PAGE_SIZE);

    // 임시 매핑 해제
    vmm_release_physical(pt_vaddr, temp_mapped);

    vmm_statistics.page_tables_count++;

    // 물리 주소 반환 (Identity mapping 범위 내에서 사용될 예정)
    return (uint32_t*)pt_paddr;
}

// =============================================================================
// 개선된 매핑 함수들
// =============================================================================

vmm_result_t vmm_map_page(uint32_t vaddr, uint32_t paddr, uint32_t flags) {
    if (!current_page_directory) return VMM_ERROR_NOT_MAPPED;

    // 주소 정렬 확인
    if ((vaddr & PAGE_OFFSET_MASK) || (paddr & PAGE_OFFSET_MASK)) {
        return VMM_ERROR_INVALID_ADDRESS;
    }

    uint32_t pd_idx = PD_INDEX(vaddr);
    uint32_t pt_idx = PT_INDEX(vaddr);

    // 페이지 디렉토리 엔트리 확인
    if (!(current_page_directory[pd_idx] & PAGE_PRESENT)) {
        uint32_t* new_pt = vmm_alloc_page_table();
        if (!new_pt) return VMM_ERROR_NO_MEMORY;

        current_page_directory[pd_idx] = ((uint32_t)new_pt & PAGE_MASK) |
                                        (flags & (PAGE_USER | PAGE_WRITE)) | PAGE_PRESENT;
    }

    // 페이지 테이블에 안전하게 접근
    uint32_t pt_paddr = current_page_directory[pd_idx] & PAGE_MASK;
    uint32_t temp_mapped;
    uint32_t* page_table = (uint32_t*)vmm_access_physical(pt_paddr, &temp_mapped);

    if (!page_table) {
        return VMM_ERROR_NO_MEMORY;
    }

    // 이미 매핑된 페이지인지 확인
    if (page_table[pt_idx] & PAGE_PRESENT) {
        vmm_release_physical(page_table, temp_mapped);
        return VMM_ERROR_ALREADY_MAPPED;
    }

    // 페이지 테이블 엔트리 설정
    page_table[pt_idx] = (paddr & PAGE_MASK) | flags | PAGE_PRESENT;

    // 임시 매핑 해제
    vmm_release_physical(page_table, temp_mapped);

    // TLB 무효화
    vmm_invalidate_page(vaddr);

    // 통계 업데이트
    vmm_statistics.mapped_pages++;
    if (vmm_is_kernel_address(vaddr)) {
        vmm_statistics.kernel_pages++;
    } else {
        vmm_statistics.user_pages++;
    }

    return VMM_SUCCESS;
}

vmm_result_t vmm_map_pages(uint32_t vaddr, uint32_t paddr, uint32_t count, uint32_t flags) {
    for (uint32_t i = 0; i < count; i++) {
        vmm_result_t result = vmm_map_page(vaddr + i * PAGE_SIZE,
                                          paddr + i * PAGE_SIZE, flags);
        if (result != VMM_SUCCESS) {
            // 롤백
            for (uint32_t j = 0; j < i; j++) {
                vmm_unmap_page(vaddr + j * PAGE_SIZE);
            }
            return result;
        }
    }
    return VMM_SUCCESS;
}

vmm_result_t vmm_unmap_page(uint32_t vaddr) {
    if (!current_page_directory) return VMM_ERROR_NOT_MAPPED;

    uint32_t pd_idx = PD_INDEX(vaddr);
    uint32_t pt_idx = PT_INDEX(vaddr);

    if (!(current_page_directory[pd_idx] & PAGE_PRESENT)) {
        return VMM_ERROR_NOT_MAPPED;
    }

    uint32_t pt_paddr = current_page_directory[pd_idx] & PAGE_MASK;
    uint32_t temp_mapped;
    uint32_t* page_table = (uint32_t*)vmm_access_physical(pt_paddr, &temp_mapped);

    if (!page_table) {
        return VMM_ERROR_NO_MEMORY;
    }

    if (!(page_table[pt_idx] & PAGE_PRESENT)) {
        vmm_release_physical(page_table, temp_mapped);
        return VMM_ERROR_NOT_MAPPED;
    }

    page_table[pt_idx] = 0;
    vmm_release_physical(page_table, temp_mapped);

    vmm_invalidate_page(vaddr);

    // 통계 업데이트
    vmm_statistics.mapped_pages--;
    if (vmm_is_kernel_address(vaddr)) {
        vmm_statistics.kernel_pages--;
    } else {
        vmm_statistics.user_pages--;
    }

    return VMM_SUCCESS;
}

vmm_result_t vmm_unmap_pages(uint32_t vaddr, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        vmm_unmap_page(vaddr + i * PAGE_SIZE);
    }
    return VMM_SUCCESS;
}

void* vmm_get_physical_address(uint32_t vaddr) {
    if (!current_page_directory) return NULL;

    uint32_t pd_idx = PD_INDEX(vaddr);
    uint32_t pt_idx = PT_INDEX(vaddr);

    if (!(current_page_directory[pd_idx] & PAGE_PRESENT)) return NULL;

    uint32_t pt_paddr = current_page_directory[pd_idx] & PAGE_MASK;
    uint32_t temp_mapped;
    uint32_t* page_table = (uint32_t*)vmm_access_physical(pt_paddr, &temp_mapped);

    if (!page_table) return NULL;

    uint32_t pte = page_table[pt_idx];
    vmm_release_physical(page_table, temp_mapped);

    if (!(pte & PAGE_PRESENT)) return NULL;

    uint32_t paddr = pte & PAGE_MASK;
    uint32_t offset = vaddr & PAGE_OFFSET_MASK;

    return (void*)(paddr + offset);
}

// =============================================================================
// 가상 주소 할당 및 관리 (기존과 동일)
// =============================================================================

uint32_t vmm_find_free_virtual_address(uint32_t start, uint32_t end, uint32_t count) {
    start = start & PAGE_MASK;
    end = end & PAGE_MASK;

    for (uint32_t addr = start; addr <= end - count * PAGE_SIZE; addr += PAGE_SIZE) {
        int found = 1;
        for (uint32_t i = 0; i < count; i++) {
            if (vmm_is_mapped(addr + i * PAGE_SIZE)) {
                found = 0;
                addr = (addr + i * PAGE_SIZE) & PAGE_MASK; // 다음 페이지부터 시작
                break;
            }
        }
        if (found) return addr;
    }

    return 0; // 찾지 못함
}

vmm_result_t vmm_alloc_virtual_pages(uint32_t vaddr, uint32_t count, vmm_alloc_type_t type) {
    if (count == 0) return VMM_ERROR_INVALID_ADDRESS;

    uint32_t flags = PAGE_FLAGS_KERNEL;
    if (type == VMM_ALLOC_USER) {
        flags = PAGE_FLAGS_USER;
    }

    for (uint32_t i = 0; i < count; i++) {
        void* paddr = pmm_alloc_frame();
        if (!paddr) {
            // 롤백
            for (uint32_t j = 0; j < i; j++) {
                void* p = vmm_get_physical_address(vaddr + j * PAGE_SIZE);
                if (p) pmm_free_frame(p);
                vmm_unmap_page(vaddr + j * PAGE_SIZE);
            }
            return VMM_ERROR_NO_MEMORY;
        }

        vmm_result_t result = vmm_map_page(vaddr + i * PAGE_SIZE, (uint32_t)paddr, flags);
        if (result != VMM_SUCCESS) {
            pmm_free_frame(paddr);
            // 롤백
            for (uint32_t j = 0; j < i; j++) {
                void* p = vmm_get_physical_address(vaddr + j * PAGE_SIZE);
                if (p) pmm_free_frame(p);
                vmm_unmap_page(vaddr + j * PAGE_SIZE);
            }
            return result;
        }
    }

    return VMM_SUCCESS;
}

// =============================================================================
// VMM 초기화 (개선됨)
// =============================================================================

void init_vmm(void) {
    kprintf("[VMM] Initializing Virtual Memory Manager...\n");

    // 통계 초기화
    memset(&vmm_statistics, 0, sizeof(vmm_stats_t));
    memset(temp_map_used, 0, sizeof(temp_map_used));

    // 1. 페이지 디렉토리 초기화
    memset(boot_page_directory, 0, sizeof(boot_page_directory));
    memset(boot_page_tables, 0, sizeof(boot_page_tables));

    // 2. Identity mapping 설정 (0~16MB)
    for (int i = 0; i < 4; i++) {
        // 페이지 테이블 설정
        for (int j = 0; j < 1024; j++) {
            uint32_t paddr = (i * 1024 + j) * PAGE_SIZE;
            if (paddr == 0) {
                boot_page_tables[i][j] = 0; // NULL 포인터 보호
            } else {
                boot_page_tables[i][j] = paddr | PAGE_FLAGS_KERNEL;
            }
        }

        // 페이지 디렉토리에 등록
        boot_page_directory[i] = (uint32_t)&boot_page_tables[i] | PAGE_FLAGS_KERNEL;
        vmm_statistics.page_tables_count++;
    }

    // 3. 커널 영역 보호 설정
    uint32_t kernel_start = (uint32_t)&_kernel_start;
    uint32_t kernel_end = (uint32_t)&_kernel_end;
    uint32_t kernel_pages = (kernel_end - kernel_start + PAGE_SIZE - 1) / PAGE_SIZE;

    kprintf("[VMM] Kernel: %x - %x (%d pages)\n",
           kernel_start, kernel_end, kernel_pages);

    // 4. 임시 매핑 영역 설정
    kprintf("[VMM] Temporary mapping area: %x - %x (%d slots)\n",
           TEMP_MAP_BASE, TEMP_MAP_BASE + TEMP_MAP_SIZE - 1, TEMP_MAP_SLOTS);

    // 5. 페이징 활성화
    __asm__ __volatile__ (
        "mov %0, %%cr3\n\t"           // CR3에 페이지 디렉토리 로드
        "mov %%cr0, %%eax\n\t"
        "or $0x80000000, %%eax\n\t"   // PG 비트 설정
        "mov %%eax, %%cr0\n\t"        // 페이징 활성화
        "jmp 1f\n\t"                  // 파이프라인 플러시
        "1:\n\t"
        : : "r" ((uint32_t)boot_page_directory)
        : "eax", "memory"
    );

    current_page_directory = boot_page_directory;

    // 초기 통계 설정
    vmm_statistics.total_virtual_pages = 1024 * 1024; // 4GB / 4KB
    vmm_statistics.mapped_pages = 4 * 1024 - 1; // 16MB - NULL page
    vmm_statistics.kernel_pages = vmm_statistics.mapped_pages;

    kprintf("[VMM] Virtual Memory Manager initialized\n");
    kprintf("[VMM] Identity mapping: 0x00000000 - %x\n", IDENTITY_MAP_SIZE - 1);
}

// =============================================================================
// 페이지 폴트 핸들러 (기존과 동일)
// =============================================================================

void page_fault_handler(uint32_t error_code) {
    uint32_t fault_address;
    __asm__ __volatile__("mov %%cr2, %0" : "=r"(fault_address));

    vmm_statistics.page_fault_count++;

    // 에러 코드 분석
    int present = !(error_code & 0x1);
    int write_fault = error_code & 0x2;
    int user_fault = error_code & 0x4;

    kprintf("[VMM] Page fault at %x (error: %x)\n", fault_address, error_code);
    kprintf("[VMM] %s, %s, %s\n",
           present ? "page not present" : "protection violation",
           write_fault ? "write" : "read",
           user_fault ? "user mode" : "kernel mode");

    // 1. 커널 힙 영역에서의 동적 할당
    if (present && fault_address >= KERNEL_HEAP_BASE && fault_address < KERNEL_VIRTUAL_BASE) {
        uint32_t page_addr = fault_address & PAGE_MASK;

        void* pframe = pmm_alloc_frame();
        if (pframe) {
            vmm_result_t result = vmm_map_page(page_addr, (uint32_t)pframe, PAGE_FLAGS_KERNEL);
            if (result == VMM_SUCCESS) {
                kprintf("[VMM] Dynamically allocated page at %x\n", page_addr);
                return; // 성공적으로 처리됨
            }
            pmm_free_frame(pframe);
        }

        kprintf("[VMM] Failed to allocate page for heap\n");
    }

    // 2. 스택 확장 (필요한 경우)
    // TODO: 스택 영역 처리

    // 3. 복구할 수 없는 오류
    char reason[64];
    if (present) {
        strcpy(reason, "PAGE NOT PRESENT");
    } else if (write_fault) {
        strcpy(reason, "WRITE PROTECTION VIOLATION");
    } else {
        strcpy(reason, "READ PROTECTION VIOLATION");
    }

    kernel_panic(reason, fault_address);
}

// =============================================================================
// 디버깅 및 통계 함수들 (기존과 동일)
// =============================================================================

vmm_stats_t vmm_get_stats(void) {
    return vmm_statistics;
}

void vmm_print_stats(void) {
    kprintf("\n=== VMM Statistics ===\n");
    kprintf("Total virtual pages: %d (%d MB)\n",
           vmm_statistics.total_virtual_pages,
           (vmm_statistics.total_virtual_pages * PAGE_SIZE) / (1024 * 1024));
    kprintf("Mapped pages:        %d (%d KB)\n",
           vmm_statistics.mapped_pages,
           (vmm_statistics.mapped_pages * PAGE_SIZE) / 1024);
    kprintf("Kernel pages:        %d (%d KB)\n",
           vmm_statistics.kernel_pages,
           (vmm_statistics.kernel_pages * PAGE_SIZE) / 1024);
    kprintf("User pages:          %d (%d KB)\n",
           vmm_statistics.user_pages,
           (vmm_statistics.user_pages * PAGE_SIZE) / 1024);
    kprintf("Page tables:         %d (%d KB)\n",
           vmm_statistics.page_tables_count,
           (vmm_statistics.page_tables_count * PAGE_SIZE) / 1024);
    kprintf("Page faults:         %d\n", vmm_statistics.page_fault_count);
    kprintf("Usage:               %d%%\n",
           (vmm_statistics.mapped_pages * 100) / vmm_statistics.total_virtual_pages);

    // 임시 매핑 슬롯 상태
    int used_slots = 0;
    for (int i = 0; i < TEMP_MAP_SLOTS; i++) {
        if (temp_map_used[i]) used_slots++;
    }
    kprintf("Temp map slots:      %d/%d used\n", used_slots, TEMP_MAP_SLOTS);
}

void vmm_dump_page_directory(void) {
    if (!current_page_directory) {
        kprintf("[VMM] No page directory loaded\n");
        return;
    }

    kprintf("\n=== Page Directory Dump ===\n");
    kprintf("Entry Present Write User Address\n");
    kprintf("===== ======= ===== ==== ========\n");

    for (int i = 0; i < 1024; i++) {
        uint32_t entry = current_page_directory[i];
        if (entry & PAGE_PRESENT) {
            kprintf("%3d   %s     %s    %s   %x\n",
                   i,
                   (entry & PAGE_PRESENT) ? "Yes" : "No ",
                   (entry & PAGE_WRITE) ? "Yes" : "No ",
                   (entry & PAGE_USER) ? "Yes" : "No ",
                   entry & PAGE_MASK);
        }
    }
}

void vmm_dump_page_table(uint32_t pd_index) {
    if (!current_page_directory || pd_index >= 1024) {
        kprintf("[VMM] Invalid page directory index\n");
        return;
    }

    uint32_t pd_entry = current_page_directory[pd_index];
    if (!(pd_entry & PAGE_PRESENT)) {
        kprintf("[VMM] Page table %d not present\n", pd_index);
        return;
    }

    uint32_t pt_paddr = pd_entry & PAGE_MASK;
    uint32_t temp_mapped;
    uint32_t* page_table = (uint32_t*)vmm_access_physical(pt_paddr, &temp_mapped);

    if (!page_table) {
        kprintf("[VMM] Failed to access page table %d\n", pd_index);
        return;
    }

    kprintf("\n=== Page Table %d Dump ===\n", pd_index);
    kprintf("Entry Virtual   Physical Present Write User\n");
    kprintf("===== ======== ======== ======= ===== ====\n");

    for (int i = 0; i < 1024; i++) {
        uint32_t entry = page_table[i];
        if (entry & PAGE_PRESENT) {
            uint32_t vaddr = (pd_index << 22) | (i << 12);
            kprintf("%3d   %x %x %s     %s    %s\n",
                   i, vaddr, entry & PAGE_MASK,
                   (entry & PAGE_PRESENT) ? "Yes" : "No ",
                   (entry & PAGE_WRITE) ? "Yes" : "No ",
                   (entry & PAGE_USER) ? "Yes" : "No ");
        }
    }

    vmm_release_physical(page_table, temp_mapped);
}

// =============================================================================
// 추가 디버깅 함수들
// =============================================================================

// 임시 매핑 상태 출력
void vmm_dump_temp_mappings(void) {
    kprintf("\n=== Temporary Mappings ===\n");
    kprintf("Slot Virtual   Physical Used\n");
    kprintf("---- -------- -------- ----\n");

    for (int i = 0; i < TEMP_MAP_SLOTS; i++) {
        uint32_t vaddr = TEMP_MAP_BASE + i * TEMP_MAP_SLOT_SIZE;
        kprintf("%2d   %x %x %s\n",
               i, vaddr, temp_map_slots[i],
               temp_map_used[i] ? "Yes" : "No ");
    }
}

// 메모리 영역별 매핑 상태 확인
void vmm_check_memory_regions(void) {
    kprintf("\n=== Memory Region Status ===\n");

    // Identity mapping 영역
    int identity_mapped = 0;
    for (uint32_t addr = 0; addr < IDENTITY_MAP_SIZE; addr += PAGE_SIZE) {
        if (vmm_is_mapped(addr)) identity_mapped++;
    }
    kprintf("Identity mapping (0 - %x): %d/%d pages mapped\n",
           IDENTITY_MAP_SIZE - 1, identity_mapped, IDENTITY_MAP_SIZE / PAGE_SIZE);

    // 커널 힙 영역
    int heap_mapped = 0;
    int heap_total = (KERNEL_VIRTUAL_BASE - KERNEL_HEAP_BASE) / PAGE_SIZE;
    for (uint32_t addr = KERNEL_HEAP_BASE; addr < KERNEL_VIRTUAL_BASE; addr += PAGE_SIZE) {
        if (vmm_is_mapped(addr)) heap_mapped++;
    }
    kprintf("Kernel heap (%x - %x): %d/%d pages mapped\n",
           KERNEL_HEAP_BASE, KERNEL_VIRTUAL_BASE - 1, heap_mapped, heap_total);

    // 유저 공간 영역
    int user_mapped = 0;
    int user_total = (KERNEL_VIRTUAL_BASE - USER_SPACE_BASE) / PAGE_SIZE;
    for (uint32_t addr = USER_SPACE_BASE; addr < KERNEL_VIRTUAL_BASE; addr += PAGE_SIZE) {
        if (vmm_is_mapped(addr)) user_mapped++;
    }
    kprintf("User space (%x - %x): %d/%d pages mapped\n",
           USER_SPACE_BASE, KERNEL_VIRTUAL_BASE - 1, user_mapped, user_total);
}