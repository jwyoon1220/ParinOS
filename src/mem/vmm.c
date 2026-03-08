//
// Created by jwyoo on 26. 3. 8..
//

#include "vmm.h"
#include "pmm.h"
#include "mem.h" // memset 사용
#include "../vga.h"     // kprintf 사용
#include "../kernel/kernel_status_manager.h"

// 커널의 현재 페이지 디렉토리 포인터
uint32_t* current_pd = NULL;
extern uint32_t _kernel_end; // 링커 스크립트의 심볼 가져오기

// ---------------------------------------------------------
// 특정 가상 주소를 물리 주소에 매핑하는 함수
// ---------------------------------------------------------
void vmm_map_page(uint32_t vaddr, uint32_t paddr, uint32_t flags) {
    uint32_t pd_idx = PD_INDEX(vaddr);
    uint32_t pt_idx = PT_INDEX(vaddr);

    // 1. 페이지 디렉토리 확인
    if (!(current_pd[pd_idx] & PAGE_PRESENT)) {
        // 새 페이지 테이블을 물리적으로 할당
        uint32_t* pt_phys = (uint32_t*)pmm_alloc_page();

        // 🌟 핵심: 물리 주소인 pt_phys를 가상 주소 영역에 등록해야 접근 가능함
        // 여기서는 초기 Identity Mapping 범위(0~16MB) 내에
        // 페이지 테이블들이 위치한다고 가정하거나, 자기 참조를 사용해야 함.

        current_pd[pd_idx] = ((uint32_t)pt_phys & PAGE_MASK) | flags | PAGE_PRESENT;

        // 새로 만든 PT를 0으로 초기화 (물리=가상인 16MB 이내라면 직접 접근 가능)
        memset(pt_phys, 0, PAGE_SIZE);
    }

    // 2. 페이지 테이블 주소 추출
    uint32_t* pt = (uint32_t*)(current_pd[pd_idx] & PAGE_MASK);

    // 3. 실제 매핑 처리
    pt[pt_idx] = (paddr & PAGE_MASK) | flags | PAGE_PRESENT;

    // 4. TLB Flush (CPU의 캐시된 이전 지도를 무효화)
    // 특정 주소만 무효화하거나 CR3를 다시 로드합니다.
    __asm__ __volatile__("invlpg (%0)" : : "r"(vaddr) : "memory");
}

// ---------------------------------------------------------
// 가상 주소가 가리키는 실제 물리 주소를 반환
// ---------------------------------------------------------
void* vmm_get_phys(uint32_t vaddr) {
    uint32_t pd_idx = PD_INDEX(vaddr);
    uint32_t pt_idx = PT_INDEX(vaddr);

    if (!(current_pd[pd_idx] & PAGE_PRESENT)) return NULL;

    uint32_t* pt = (uint32_t*)(current_pd[pd_idx] & PAGE_MASK);
    if (!(pt[pt_idx] & PAGE_PRESENT)) return NULL;

    uint32_t paddr = pt[pt_idx] & PAGE_MASK;
    uint32_t offset = vaddr & 0xFFF;

    return (void*)(paddr + offset);
}

// ---------------------------------------------------------
// VMM 초기화 및 페이징 활성화
// ---------------------------------------------------------
uint32_t boot_pd[1024] __attribute__((aligned(4096)));
uint32_t boot_pt[4][1024] __attribute__((aligned(4096))); // 16MB 분량 (4개 테이블)
void init_vmm() {
    __asm__ __volatile__ (
        // ---------------------------------------------------
        // 1. boot_pd 1024칸(4096 바이트)을 0으로 싹 밀기
        // ---------------------------------------------------
        "mov %0, %%edi\n\t"           // edi = boot_pd 주소
        "mov $1024, %%ecx\n\t"        // 루프 카운트 = 1024
        "xor %%eax, %%eax\n\t"        // eax = 0
        "rep stosl\n\t"               // boot_pd를 0으로 채움

        // ---------------------------------------------------
        // 2. boot_pt 4096칸(16MB 분량)을 0으로 밀기
        // ---------------------------------------------------
        "mov %1, %%edi\n\t"           // edi = boot_pt 주소
        "mov $4096, %%ecx\n\t"        // 루프 카운트 = 4096 (1024 * 4)
        "xor %%eax, %%eax\n\t"
        "rep stosl\n\t"               // boot_pt를 0으로 채움

        // ---------------------------------------------------
        // 3. boot_pt에 0MB~16MB 물리 주소 매핑 (Present=1, RW=1 -> 0x03)
        // ---------------------------------------------------
        "mov %1, %%edi\n\t"           // edi = boot_pt
        "mov $0x00000003, %%eax\n\t"  // eax = 0x00000000 | 0x03
        "mov $4096, %%ecx\n\t"        // 16MB를 4KB로 나누면 4096개 엔트리
    ".Lfill_pt:\n\t"
        "mov %%eax, (%%edi)\n\t"      // boot_pt[i] = eax
        "add $4096, %%eax\n\t"        // 물리 주소 4KB(0x1000) 증가
        "add $4, %%edi\n\t"           // 다음 엔트리로 이동 (32비트 = 4바이트)
        "loop .Lfill_pt\n\t"

        // ---------------------------------------------------
        // 🌟 3.5. Null Pointer (0x0) 방어벽 설치!
        // 0번지는 매핑을 해제해서 접근 시 무조건 패닉이 나게 함
        // ---------------------------------------------------
        "mov %1, %%edi\n\t"
        "movl $0, 0(%%edi)\n\t"       // boot_pt[0] = 0 (Present = 0)

        // ---------------------------------------------------
        // 4. boot_pd에 4개의 페이지 테이블(boot_pt) 등록
        // ---------------------------------------------------
        "mov %0, %%edi\n\t"           // edi = boot_pd
        "mov %1, %%eax\n\t"           // eax = boot_pt
        "or $0x00000003, %%eax\n\t"   // eax = boot_pt | 0x03

        "mov %%eax, 0(%%edi)\n\t"      // boot_pd[0] 등록 (0~4MB)
        "add $4096, %%eax\n\t"         // 다음 페이지 테이블 주소로 (+4KB)
        "mov %%eax, 4(%%edi)\n\t"      // boot_pd[1] 등록 (4~8MB)
        "add $4096, %%eax\n\t"
        "mov %%eax, 8(%%edi)\n\t"      // boot_pd[2] 등록 (8~12MB)
        "add $4096, %%eax\n\t"
        "mov %%eax, 12(%%edi)\n\t"     // boot_pd[3] 등록 (12~16MB)

        // ---------------------------------------------------
        // 5. CR3 탑재 및 CR0 페이징 켜기 (TLB 완전 플러시 포함)
        // ---------------------------------------------------
        "mov %0, %%cr3\n\t"           // CR3에 페이지 디렉토리 탑재
        "mov %%cr0, %%eax\n\t"
        "or $0x80000000, %%eax\n\t"   // PG 비트(31번) 세트
        "mov %%eax, %%cr0\n\t"        // 페이징 활성화 발사!

        "jmp .Lflush\n"               // 파이프라인 정리 (Far Jump 역할)
    ".Lflush:\n\t"
        "mov %%cr3, %%eax\n\t"        // TLB(주소 캐시) 강제 무효화
        "mov %%eax, %%cr3\n\t"

        : /* 출력 없음 */
        : "r" ((uint32_t)boot_pd), "r" ((uint32_t)boot_pt) // C 변수 주소 입력
        : "eax", "ecx", "edi", "memory", "cc" // 오염되는 레지스터들 (컴파일러 경고용)
    );

    current_pd = boot_pd;
    kprintf("[VMM] Pure ASM Paging ON! Address 0x0 is UNMAPPED.\n");
}

// vmm.c 또는 kernel_status_manager.c
void page_fault_handler(uint32_t error_code) {
    uint32_t faulting_address;
    __asm__ __volatile__("mov %%cr2, %0" : "=r" (faulting_address));


    // 에러 코드 분석 (비트 단위)
    int present = !(error_code & 0x1); // 페이지가 존재하지 않아서 발생했는가?
    int write = error_code & 0x2;      // 쓰기 시도 중에 발생했는가?
    int user = error_code & 0x4;       // 유저 모드에서 발생했는가?

    // 🌟 동적 매핑의 핵심: 힙 영역(예: 0x800000 이상)에서 발생한
    // "Present" 에러라면 즉석에서 메모리를 할당해 줍니다.
    if (present && faulting_address >= 0x1000000) {
        uint32_t* new_page = (uint32_t*)pmm_alloc_page();
        if (new_page) {
            vmm_map_page(faulting_address & PAGE_MASK, (uint32_t)new_page, 0x03);
            // 성공적으로 매핑했으니 핸들러를 종료하고 원래 명령어로 돌아갑니다.
            return;
        }
    }

    // 만약 매핑에 실패했거나, 커널 코드 영역을 건드렸다면 패닉!
    char* reason = present ? "PAGE NOT PRESENT" : "READ-ONLY VIOLATION";
    kernel_panic(reason, faulting_address);
}
