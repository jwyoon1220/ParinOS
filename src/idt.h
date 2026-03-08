#ifndef IDT_H
#define IDT_H

#include <stdint.h>

#pragma pack(push, 1)

// 🌟 인터럽트 발생 시 CPU 레지스터 상태를 담는 구조체
typedef struct {
    uint32_t ds;                                     // Data segment selector
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax; // Pushed by pusha
    uint32_t int_no, err_code;                       // Interrupt number and error code
    uint32_t eip, cs, eflags, useresp, ss;           // Pushed by the processor automatically
} registers_t;

struct idt_entry_struct {
    uint16_t base_low;  // 핸들러 주소 하위 16비트
    uint16_t sel;       // 커널 코드 세그먼트 (0x08)
    uint8_t  always0;   // 항상 0
    uint8_t  flags;     // 속성 (0x8E: Interrupt Gate, Ring 0)
    uint16_t base_high; // 핸들러 주소 상위 16비트
};
typedef struct idt_entry_struct idt_entry_t;

struct idt_ptr_struct {
    uint16_t limit;
    uint32_t base;
};



typedef struct idt_ptr_struct idt_ptr_t;
#pragma pack(pop)

void init_idt();
void timer_handler_main();
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);


#endif