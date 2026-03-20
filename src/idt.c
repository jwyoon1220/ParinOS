#include "idt.h"
#include "vga.h"
#include "io.h"
#include "keyboard.h"
#include "kernel/kernel_status_manager.h"
#include "mem/pmm.h"
#include "user/user.h"

// 정리된 src/idt.c

#include "idt.h"
#include "vga.h"
#include "io.h"

idt_entry_t idt[256];
idt_ptr_t   idt_ptr;

extern void idt_load(uint32_t idt_ptr_addr);
extern void irq0_handler();
extern void irq1_handler();
extern void isr14_handler();
extern void syscall_handler();

void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low = base & 0xFFFF;
    idt[num].base_high = (base >> 16) & 0xFFFF;
    idt[num].sel = sel;
    idt[num].always0 = 0;
    idt[num].flags = flags;
}

void init_idt() {
    // PIC 리매핑
    outb(0x20, 0x11); outb(0xA0, 0x11);
    outb(0x21, 0x20); outb(0xA1, 0x28);
    outb(0x21, 0x04); outb(0xA1, 0x02);
    outb(0x21, 0x01); outb(0xA1, 0x01);
    outb(0x21, 0x0);  outb(0xA1, 0x0);

    idt_ptr.limit = (sizeof(idt_entry_t) * 256) - 1;
    idt_ptr.base  = (uint32_t)&idt;

    idt_set_gate(14, (uint32_t)isr14_handler, 0x08, 0x8E);

    // IRQ0(타이머)와 IRQ1(키보드) 핸들러 등록
    idt_set_gate(32, (uint32_t)irq0_handler, 0x08, 0x8E);
    idt_set_gate(33, (uint32_t)irq1_handler, 0x08, 0x8E);

    // int 0x80: 시스템 콜 (DPL=3 → Ring 3 유저 코드에서 호출 가능)
    // flags = 0xEE = Present|DPL3|32-bit Interrupt Gate
    idt_set_gate(0x80, (uint32_t)syscall_handler, 0x08, 0xEE);

    idt_load((uint32_t)&idt_ptr);

    // 시스템 콜 인터페이스 초기화
    syscall_init();
}
