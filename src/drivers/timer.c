//
// Created by jwyoo on 26. 3. 8..
//

#include "timer.h"
#include "../idt.h"
#include "../io.h"
#include "../vga.h"
#include "../drivers/serial.h"

volatile uint32_t tick = 0;

// irq0_handler에서 호출되는거
void timer_handler_main(registers_t* regs) {
    (void)regs;
    tick++;
    //kprintf_serial("tick: %d\n", tick);
    outb(0x20, 0x20); // 인터럽트 끝
}

void init_timer(uint32_t frequency) {
    uint32_t divisor = 1193182 / frequency;
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
    kprintf("[PIT] Timer initialized at %d Hz\n", frequency);
}

void sleep(uint32_t ms) {
    __asm__ __volatile__("sti");
    uint32_t end_tick = tick + ms;
    while (tick < end_tick) {
        __asm__ __volatile__("hlt");
    }
}