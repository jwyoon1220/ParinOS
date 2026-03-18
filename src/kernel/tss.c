//
// src/kernel/tss.c
// Task State Segment (TSS) 구현
//

#include "tss.h"
#include "../vga.h"

// 전역 TSS 인스턴스 (단일 프로세서, 단일 TSS)
static tss_t tss;

void tss_init(uint32_t kernel_ss, uint32_t kernel_esp) {
    // TSS 전체를 0으로 초기화
    for (int i = 0; i < (int)sizeof(tss_t); i++)
        ((uint8_t*)&tss)[i] = 0;

    tss.ss0  = kernel_ss;
    tss.esp0 = kernel_esp;

    // I/O 권한 비트맵을 TSS 바깥으로 설정 → 유저가 모든 포트 접근 시 #GP 발생
    tss.iomap_base = (uint16_t)sizeof(tss_t);

    kprintf("[TSS] Initialized: ss0=0x%x esp0=0x%x\n", kernel_ss, kernel_esp);
}

void tss_set_kernel_stack(uint32_t new_esp0) {
    tss.esp0 = new_esp0;
}

tss_t* tss_get(void) {
    return &tss;
}

void tss_load(uint16_t selector) {
    __asm__ __volatile__("ltr %0" : : "r"(selector));
    kprintf("[TSS] Loaded selector=0x%x\n", (uint32_t)selector);
}
