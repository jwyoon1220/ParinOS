#ifndef PARINOS_TSS_H
#define PARINOS_TSS_H

#include <stdint.h>

// ─── Task State Segment (TSS) ───────────────────────────────────────────────
// x86 32비트 TSS 구조체 (하드웨어 정의 레이아웃)
// Ring 3 → Ring 0 전환 시 CPU가 이 구조체에서 커널 스택 포인터(esp0/ss0)를 읽음

#pragma pack(push, 1)
typedef struct {
    uint32_t prev_tss;   // 이전 TSS 선택자 (하드웨어 태스크 전환 시 사용, 우리는 안 씀)
    uint32_t esp0;       // Ring 0 스택 포인터 (유저→커널 전환 시 CPU가 ESP를 여기서 가져옴)
    uint32_t ss0;        // Ring 0 스택 세그먼트 (커널 데이터 세그먼트 = 0x10)
    uint32_t esp1;       // Ring 1 스택 (사용 안 함)
    uint32_t ss1;
    uint32_t esp2;       // Ring 2 스택 (사용 안 함)
    uint32_t ss2;
    uint32_t cr3;        // 페이지 디렉토리 (소프트웨어 태스크 전환용, 우리는 안 씀)
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax, ecx, edx, ebx;
    uint32_t esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs;
    uint32_t ldt;        // LDT 선택자 (사용 안 함)
    uint16_t trap;       // 디버그 트랩 (사용 안 함)
    uint16_t iomap_base; // I/O 권한 맵 오프셋 (sizeof(tss_entry_t) → 비트맵 없음)
} tss_entry_t;
#pragma pack(pop)

// 전역 TSS 인스턴스 (gdt.c에서 GDT 엔트리 설정 시 주소 사용)
extern tss_entry_t tss;

// TSS 초기화: ss0 = 0x10, esp0 = 0 (커널 스택은 스레드 생성 시 설정)
void tss_init(void);

// 유저 스레드 전환 직전, 해당 스레드의 커널 스택 상단을 TSS.esp0에 설정
// 이 값은 Ring 3에서 인터럽트 발생 시 CPU가 커널 스택을 잡는 기준점이 됨
void tss_set_kernel_stack(uint32_t esp0);

#endif // PARINOS_TSS_H
