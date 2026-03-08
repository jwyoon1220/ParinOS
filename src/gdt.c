#include "gdt.h"

// 3개의 GDT 엔트리 생성: 0번(Null), 1번(코드), 2번(데이터)
gdt_entry_t gdt_entries[3];
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

void init_gdt() {
    gdt_ptr.limit = (sizeof(gdt_entry_t) * 3) - 1;
    gdt_ptr.base  = (uint32_t)&gdt_entries;

    // 1. Null 세그먼트 (항상 0번이어야 함)
    gdt_set_gate(0, 0, 0, 0, 0);                
    
    // 2. 커널 코드 세그먼트 (1번, 오프셋 0x08)
    // - Base: 0, Limit: 4GB(0xFFFFFFFF)
    // - Access: 0x9A (실행/읽기 가능, Ring 0)
    // - Granularity: 0xCF (4KB 단위, 32비트 모드)
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF); 
    
    // 3. 커널 데이터 세그먼트 (2번, 오프셋 0x10)
    // - Access: 0x92 (읽기/쓰기 가능, Ring 0)
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF); 

    // 새로운 GDT를 CPU에 등록!
    gdt_flush((uint32_t)&gdt_ptr);
}