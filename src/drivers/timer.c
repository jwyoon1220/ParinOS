//
// Created by jwyoon on 26. 3. 8..
//

#include "timer.h"
#include "../idt.h"
#include "../io.h"
#include "../vga.h"
#include "../drivers/serial.h"
#include "../util/util.h"

// 시스템 틱 카운터 (scheduler_tick 에서 증가, sleep/get_total_ticks 에서 사용)
volatile uint32_t tick = 0;

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

/**
 * 시스템 부팅 후 발생한 총 타이머 틱(ms)을 반환합니다.
 */
uint32_t get_total_ticks() {
    uint32_t res;
    asm volatile("cli");    // 인터럽트 금지 (값 보호)
    res = tick;
    asm volatile("sti");    // 인터럽트 허용
    return res;
}
