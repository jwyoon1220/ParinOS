//
// src/kernel/tss.h
// Task State Segment (TSS) - Ring 0 <-> Ring 3 전환을 위한 커널 스택 정보 제공
//

#ifndef PARINOS_TSS_H
#define PARINOS_TSS_H

#include <stdint.h>

// ─────────────────────────────────────────────────────────────────────────────
// x86 TSS 구조체 (하드웨어 정의, 104 바이트)
// ─────────────────────────────────────────────────────────────────────────────
#pragma pack(push, 1)
typedef struct {
    uint32_t prev_tss;   // 이전 TSS 링크 (하드웨어 태스크 전환 시 사용, 여기서는 0)
    uint32_t esp0;       // Ring 0 스택 포인터 (Ring 3 → Ring 0 전환 시 CPU가 로드)
    uint32_t ss0;        // Ring 0 스택 세그먼트 (0x10 = 커널 데이터 세그먼트)
    uint32_t esp1;       // Ring 1 스택 (사용 안 함)
    uint32_t ss1;
    uint32_t esp2;       // Ring 2 스택 (사용 안 함)
    uint32_t ss2;
    uint32_t cr3;        // 페이지 디렉터리 기저 주소 (여기서는 0)
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax, ecx, edx, ebx;
    uint32_t esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base; // I/O 권한 비트맵 오프셋 (sizeof(tss_t) → 맵 없음)
} tss_t;
#pragma pack(pop)

// ─────────────────────────────────────────────────────────────────────────────
// TSS 초기화 및 업데이트 API
// ─────────────────────────────────────────────────────────────────────────────

/**
 * TSS를 초기화합니다. GDT 설정 이후 호출해야 합니다.
 * @param kernel_ss   Ring 0 스택 세그먼트 (보통 0x10)
 * @param kernel_esp  Ring 0 초기 스택 포인터
 */
void tss_init(uint32_t kernel_ss, uint32_t kernel_esp);

/**
 * 스케줄러가 커널 스레드를 선택할 때 TSS.esp0를 갱신합니다.
 * Ring 3 태스크가 인터럽트/시스콜 발생 시 이 스택으로 전환됩니다.
 *
 * @param new_esp0  새 커널 스택 포인터 (kthread_t.stack + stack_size)
 */
void tss_set_kernel_stack(uint32_t new_esp0);

/**
 * 전역 TSS 구조체 포인터 반환 (GDT 디스크립터 설정에 사용)
 */
tss_t* tss_get(void);

/**
 * `ltr` 명령어로 TSS를 CPU에 로드합니다.
 * GDT에 TSS 디스크립터가 등록된 후 호출해야 합니다.
 *
 * @param selector  GDT에서 TSS 디스크립터의 세그먼트 선택자 (예: 0x28)
 */
void tss_load(uint16_t selector);

#endif // PARINOS_TSS_H
