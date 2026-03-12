#include "gdt.h"
#include "kernel/tss.h"

// 6개 GDT 엔트리: Null, 커널 코드/데이터, 유저 코드/데이터, TSS
gdt_entry_t gdt_entries[6];
gdt_ptr_t   gdt_ptr;

extern void gdt_flush(uint32_t gdt_ptr_addr);
extern void tss_flush(uint16_t sel);

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

void init_gdt(void) {
    gdt_ptr.limit = (sizeof(gdt_entry_t) * 6) - 1;
    gdt_ptr.base  = (uint32_t)&gdt_entries;

    // 0. Null 세그먼트 (항상 0번이어야 함)
    gdt_set_gate(0, 0, 0, 0, 0);

    // 1. 커널 코드 세그먼트 (오프셋 0x08, Ring 0)
    //    Access: 0x9A = Present, DPL=0, S=1, Type=1010(Code+Execute+Read)
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);

    // 2. 커널 데이터 세그먼트 (오프셋 0x10, Ring 0)
    //    Access: 0x92 = Present, DPL=0, S=1, Type=0010(Data+Write)
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);

    // 3. 유저 코드 세그먼트 (오프셋 0x18, Ring 3, 선택자 0x1B = 0x18|3)
    //    Access: 0xFA = Present, DPL=3, S=1, Type=1010(Code+Execute+Read)
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xCF);

    // 4. 유저 데이터 세그먼트 (오프셋 0x20, Ring 3, 선택자 0x23 = 0x20|3)
    //    Access: 0xF2 = Present, DPL=3, S=1, Type=0010(Data+Write)
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);

    // 5. TSS (Task State Segment) 디스크립터 (오프셋 0x28, Ring 0)
    //    Access: 0x89 = Present, DPL=0, S=0(System), Type=1001(32-bit TSS Available)
    //    Granularity: 0x00 (바이트 단위, 4KB 아님)
    //    Limit: sizeof(tss_entry_t) - 1 (I/O 비트맵 없음 — tss.iomap_base가
    //    TSS 한계를 초과하면 CPU가 모든 I/O 포트 접근을 Ring 3에서 금지함.
    //    Intel 매뉴얼 Vol.3 §19.5.2: "I/O bit map base >= segment limit+1"인
    //    경우 I/O 비트맵이 없는 것으로 처리됩니다.)
    tss_init();
    gdt_set_gate(5, (uint32_t)&tss, sizeof(tss_entry_t) - 1, 0x89, 0x00);

    // GDT를 CPU에 등록하고 TSS 로드
    gdt_flush((uint32_t)&gdt_ptr);
    tss_flush(GDT_TSS_SEG);
}