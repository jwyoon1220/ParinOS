#include "vga.h"
#include "gdt.h"
#include "idt.h"
#include "keyboard.h"
#include "mem/pmm.h"
#include "drivers/serial.h"
#include "drivers/timer.h"
#include "std/malloc.h"
#include "shell/shell.h"
#include "mem/vmm.h"
#include "drivers/pci.h"
#include "drivers/ahci.h"


void kmain() {
    vga_clear();

    init_gdt();   // GDT 초기화
    init_idt();   // IDT 초기화

    init_pmm();
    init_vmm();
    init_heap(0x800000, 10);

    init_pci();   // PCI 초기화
    init_ahci();

    init_timer(1000);

    init_serial();


    __asm__ __volatile__("sti");

    init_keyboard();
    shell_init();

    while(1) {
        // CPU 대기 상태 (전력 소모 감소)
        __asm__("hlt"); 
    }
}
