//
// src/hal/hal.c — Hardware Abstraction Layer 구현
//

#include "hal.h"
#include "vga.h"
#include "../drivers/timer.h"

/* ── 시스템 리셋 ─────────────────────────────────────────────────────────── */
void hal_reboot(void) {
    /* 키보드 컨트롤러 CPU 리셋 라인 */
    hal_cli();
    uint8_t tmp;
    /* 출력 버퍼 비우기 */
    do { tmp = hal_inb(0x64); } while (tmp & 0x01);
    hal_outb(0x64, 0xFE);  /* 리셋 펄스 */
    /* 실패 시 트리플 폴트 */
    hal_cli();
    hal_halt();
    while (1) {}
}

/* ── PIT 초기화 ─────────────────────────────────────────────────────────── */
void hal_pit_init(uint32_t hz) {
    init_timer(hz);
}

uint32_t hal_get_ticks(void) {
    return get_total_ticks();
}

void hal_delay_ms(uint32_t ms) {
    uint32_t end = hal_get_ticks() + ms;
    while (hal_get_ticks() < end) {
        hal_halt();
    }
}

/* ── PIC 8259A ────────────────────────────────────────────────────────────── */
#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI   0x20
#define PIC_ICW1  0x11   /* ICW1: cascade, ICW4 needed */
#define PIC_ICW4  0x01   /* ICW4: 8086 mode */

void hal_pic_init(uint8_t master_offset, uint8_t slave_offset) {
    /* ICW1 */
    hal_outb(PIC1_CMD,  PIC_ICW1);
    hal_io_wait();
    hal_outb(PIC2_CMD,  PIC_ICW1);
    hal_io_wait();

    /* ICW2: 벡터 오프셋 */
    hal_outb(PIC1_DATA, master_offset);
    hal_io_wait();
    hal_outb(PIC2_DATA, slave_offset);
    hal_io_wait();

    /* ICW3 */
    hal_outb(PIC1_DATA, 0x04); /* 슬레이브가 IRQ2 에 연결 */
    hal_io_wait();
    hal_outb(PIC2_DATA, 0x02);
    hal_io_wait();

    /* ICW4 */
    hal_outb(PIC1_DATA, PIC_ICW4);
    hal_io_wait();
    hal_outb(PIC2_DATA, PIC_ICW4);
    hal_io_wait();

    /* 모든 IRQ 허용 */
    hal_outb(PIC1_DATA, 0x00);
    hal_outb(PIC2_DATA, 0x00);
}

void hal_pic_set_mask(uint8_t irq, bool masked) {
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    uint8_t  bit  = (irq < 8) ? irq : (uint8_t)(irq - 8);
    uint8_t  val  = hal_inb(port);
    if (masked) val |=  (uint8_t)(1u << bit);
    else        val &= (uint8_t)~(uint8_t)(1u << bit);
    hal_outb(port, val);
}

void hal_pic_send_eoi(uint8_t irq) {
    if (irq >= 8) hal_outb(PIC2_CMD, PIC_EOI);
    hal_outb(PIC1_CMD, PIC_EOI);
}

/* ── HAL 초기화 ─────────────────────────────────────────────────────────── */
void hal_init(void) {
    klog_info("[HAL] Hardware Abstraction Layer initialized\n");
}
