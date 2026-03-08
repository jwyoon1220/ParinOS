//
// Created by jwyoo on 26. 3. 8..
//

#include "kernel_status_manager.h"

#include <stdint.h>
#include "../vga.h"
#include "../io.h"
#include "../drivers/timer.h"
#include "../mem/pmm.h"


void kernel_panic(char* reason, uint32_t addr) {
    // 2. 화면 전체를 파란색 공백으로 초기화
    vga_clear();

    // 3. 고전 BSOD 문구 출력 (공백과 줄바꿈으로 레이아웃 조정)
    kprintf("\nKernel Panic: %s\n", reason);
    kprintf("At %x\n", addr);
    kprintf("\n");
    kprintf("nSystem is restart in 10 seconds.\n\n");

    for (int i = 10; i > 0; i--) {
        kprintf(".");
        sleep(1000); // 1초 대기
    }

    uint8_t good = 0x02;
    while (good & 0x02) {
        good = inb(0x64);
    }
    outb(0x64, 0xFE);
}
