//
// src/hal/hal.h — Hardware Abstraction Layer
//
// 하드웨어에 직접 의존하는 저수준 인터페이스를 추상화합니다.
// 상위 계층(드라이버, 커널)은 이 헤더만 사용하면 됩니다.
//

#ifndef PARINOS_HAL_H
#define PARINOS_HAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ════════════════════════════════════════════════════════════════════════════
 * I/O 포트
 * ════════════════════════════════════════════════════════════════════════════ */

/** 8비트 I/O 포트 읽기 */
static inline uint8_t hal_inb(uint16_t port) {
    uint8_t v;
    asm volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

/** 16비트 I/O 포트 읽기 */
static inline uint16_t hal_inw(uint16_t port) {
    uint16_t v;
    asm volatile("inw %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

/** 32비트 I/O 포트 읽기 */
static inline uint32_t hal_inl(uint16_t port) {
    uint32_t v;
    asm volatile("inl %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

/** 8비트 I/O 포트 쓰기 */
static inline void hal_outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

/** 16비트 I/O 포트 쓰기 */
static inline void hal_outw(uint16_t port, uint16_t val) {
    asm volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

/** 32비트 I/O 포트 쓰기 */
static inline void hal_outl(uint16_t port, uint32_t val) {
    asm volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

/** I/O 지연 (약 1-4μs) */
static inline void hal_io_wait(void) {
    hal_outb(0x80, 0x00);
}

/* ════════════════════════════════════════════════════════════════════════════
 * 인터럽트 제어
 * ════════════════════════════════════════════════════════════════════════════ */

/** 인터럽트 활성화 */
static inline void hal_sti(void) { asm volatile("sti"); }

/** 인터럽트 비활성화 */
static inline void hal_cli(void) { asm volatile("cli"); }

/** 현재 EFLAGS 에서 IF(Interrupt Flag) 비트 반환 */
static inline bool hal_interrupts_enabled(void) {
    uint32_t flags;
    asm volatile("pushf; pop %0" : "=r"(flags));
    return (flags & (1u << 9)) != 0;
}

/** 인터럽트를 잠시 끄고 이전 상태를 저장 */
static inline uint32_t hal_irq_save(void) {
    uint32_t flags;
    asm volatile("pushf; pop %0; cli" : "=r"(flags));
    return flags;
}

/** hal_irq_save 로 저장한 인터럽트 상태 복원 */
static inline void hal_irq_restore(uint32_t flags) {
    asm volatile("push %0; popf" : : "r"(flags) : "cc");
}

/* ════════════════════════════════════════════════════════════════════════════
 * CPU 정보 / 레지스터
 * ════════════════════════════════════════════════════════════════════════════ */

/** CPUID 래퍼 */
static inline void hal_cpuid(uint32_t leaf,
                              uint32_t *eax, uint32_t *ebx,
                              uint32_t *ecx, uint32_t *edx) {
    asm volatile("cpuid"
                 : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                 : "a"(leaf));
}

/** CR0 레지스터 읽기 */
static inline uint32_t hal_read_cr0(void) {
    uint32_t v; asm volatile("mov %%cr0, %0" : "=r"(v)); return v;
}

/** CR2 레지스터 읽기 (페이지 폴트 주소) */
static inline uint32_t hal_read_cr2(void) {
    uint32_t v; asm volatile("mov %%cr2, %0" : "=r"(v)); return v;
}

/** CR3 레지스터 읽기 (페이지 디렉터리 물리 주소) */
static inline uint32_t hal_read_cr3(void) {
    uint32_t v; asm volatile("mov %%cr3, %0" : "=r"(v)); return v;
}

/** CR3 레지스터 쓰기 (TLB 플러시) */
static inline void hal_write_cr3(uint32_t val) {
    asm volatile("mov %0, %%cr3" : : "r"(val) : "memory");
}

/** CR0 레지스터 쓰기 */
static inline void hal_write_cr0(uint32_t val) {
    asm volatile("mov %0, %%cr0" : : "r"(val) : "memory");
}

/** TLB 단일 페이지 무효화 */
static inline void hal_invlpg(uint32_t vaddr) {
    asm volatile("invlpg (%0)" : : "r"(vaddr) : "memory");
}

/** RDTSC — 타임스탬프 카운터 */
static inline uint64_t hal_rdtsc(void) {
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

/** CPU 정지 (hlt) */
static inline void hal_halt(void) { asm volatile("hlt"); }

/** 소프트웨어 중단점 (int3) */
static inline void hal_breakpoint(void) { asm volatile("int3"); }

/** 시스템 리셋 (키보드 컨트롤러 통한 CPU 리셋) */
void hal_reboot(void);

/* ════════════════════════════════════════════════════════════════════════════
 * 타이머 (PIT 8253/8254)
 * ════════════════════════════════════════════════════════════════════════════ */

/** PIT 을 hz 주파수로 초기화 */
void hal_pit_init(uint32_t hz);

/** 현재 타이머 틱 수 (init 이후 누적) */
uint32_t hal_get_ticks(void);

/** ms 밀리초 동안 바쁜 대기 */
void hal_delay_ms(uint32_t ms);

/* ════════════════════════════════════════════════════════════════════════════
 * PIC (8259A)
 * ════════════════════════════════════════════════════════════════════════════ */

/** 마스터/슬레이브 PIC 재초기화 */
void hal_pic_init(uint8_t master_offset, uint8_t slave_offset);

/** IRQ 마스크 설정 (0=허용, 1=차단) */
void hal_pic_set_mask(uint8_t irq, bool masked);

/** EOI(End Of Interrupt) 전송 */
void hal_pic_send_eoi(uint8_t irq);

/* ════════════════════════════════════════════════════════════════════════════
 * MMIO 헬퍼 (메모리 맵 레지스터 읽기/쓰기, 캐시 우회)
 * ════════════════════════════════════════════════════════════════════════════ */

static inline uint8_t  hal_mmio_read8 (uintptr_t addr) {
    return *(volatile uint8_t *)addr;
}
static inline uint16_t hal_mmio_read16(uintptr_t addr) {
    return *(volatile uint16_t *)addr;
}
static inline uint32_t hal_mmio_read32(uintptr_t addr) {
    return *(volatile uint32_t *)addr;
}
static inline void hal_mmio_write8 (uintptr_t addr, uint8_t  v) {
    *(volatile uint8_t *)addr  = v;
}
static inline void hal_mmio_write16(uintptr_t addr, uint16_t v) {
    *(volatile uint16_t *)addr = v;
}
static inline void hal_mmio_write32(uintptr_t addr, uint32_t v) {
    *(volatile uint32_t *)addr = v;
}

/* ════════════════════════════════════════════════════════════════════════════
 * HAL 초기화
 * ════════════════════════════════════════════════════════════════════════════ */

/** HAL 전체 초기화 (kernel.c 의 kmain 에서 가장 먼저 호출) */
void hal_init(void);

#endif /* PARINOS_HAL_H */
