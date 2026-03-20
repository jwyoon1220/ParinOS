#include "gdt.h"
#include "kernel/tss.h"

// 6개의 GDT 엔트리:
//  0: Null
//  1: 커널 코드  (Ring 0, 0x08)
//  2: 커널 데이터 (Ring 0, 0x10)
//  3: 유저 코드  (Ring 3, 0x18)
//  4: 유저 데이터 (Ring 3, 0x20)
//  5: TSS        (0x28)
gdt_entry_t gdt_entries[6];
gdt_ptr_t   gdt_ptr;

// 어셈블리로 작성할 GDT 로드 함수 선언
extern void gdt_flush(uint32_t gdt_ptr_addr);

// GDT 엔트리 값을 세팅하는 내부 함수
static void gdt_set_gate(int32_t num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt_entries[num].base_low    = (base & 0xFFFF);
    gdt_entries[num].base_middle = (base >> 16) & 0xFF;
    gdt_entries[num].base_high   = (base >> 24) & 0xFF;

    gdt_entries[num].limit_low   = (limit & 0xFFFF);
    gdt_entries[num].granularity = (limit >> 16) & 0x0F;

    gdt_entries[num].granularity |= gran & 0xF0;
    gdt_entries[num].access      = access;
}

// TSS 디스크립터 설정 (System Segment, type=0x89 = 32-bit TSS Available)
static void gdt_set_tss(int32_t num, uint32_t base, uint32_t limit) {
    gdt_entries[num].base_low    = (base & 0xFFFF);
    gdt_entries[num].base_middle = (base >> 16) & 0xFF;
    gdt_entries[num].base_high   = (base >> 24) & 0xFF;

    gdt_entries[num].limit_low   = (limit & 0xFFFF);
    gdt_entries[num].granularity = ((limit >> 16) & 0x0F); // G=0, 16-bit limit

    // Access: Present=1, DPL=0, S=0(System), Type=1001(32-bit TSS Available)
    gdt_entries[num].access = 0x89;
}

void init_gdt() {
    gdt_ptr.limit = (sizeof(gdt_entry_t) * 6) - 1;
    gdt_ptr.base  = (uint32_t)&gdt_entries;

    // 0. Null 세그먼트
    gdt_set_gate(0, 0, 0, 0, 0);

    // 1. 커널 코드 세그먼트 (Ring 0, 0x08)
    //    Access: 0x9A = Present|DPL0|S|Exec|Read
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);

    // 2. 커널 데이터 세그먼트 (Ring 0, 0x10)
    //    Access: 0x92 = Present|DPL0|S|Write
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);

    // 3. 유저 코드 세그먼트 (Ring 3, 0x18)
    //    Access: 0xFA = Present|DPL3|S|Exec|Read
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xCF);

    // 4. 유저 데이터 세그먼트 (Ring 3, 0x20)
    //    Access: 0xF2 = Present|DPL3|S|Write
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);

    // 5. TSS 디스크립터 (0x28) - tss_init() 호출 후 크기/주소 설정
    tss_init(0x10, 0); // 커널 데이터 세그먼트, esp0는 나중에 업데이트됨
    gdt_set_tss(5, (uint32_t)tss_get(), sizeof(tss_t) - 1);

    // 새로운 GDT를 CPU에 등록
    gdt_flush((uint32_t)&gdt_ptr);

    // TSS 로드
    tss_load(0x28);
}