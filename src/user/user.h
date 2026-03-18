//
// src/user/user.h
// 유저 모드(Ring 3) 기반 구조체 및 API 정의
//
// 구조:
//   - 유저 프로세스마다 독립적인 가상 주소 공간
//   - 시스템 콜은 int 0x80 (Linux 호환 인터페이스)
//   - ELF32 바이너리 실행 가능
//

#ifndef PARINOS_USER_H
#define PARINOS_USER_H

#include <stdint.h>
#include <stddef.h>

// ─────────────────────────────────────────────────────────────────────────────
// 세그먼트 선택자 상수
// ─────────────────────────────────────────────────────────────────────────────
#define SEG_KERNEL_CODE  0x08  // GDT 엔트리 1, Ring 0
#define SEG_KERNEL_DATA  0x10  // GDT 엔트리 2, Ring 0
#define SEG_USER_CODE    0x1B  // GDT 엔트리 3 | RPL=3 (0x18 | 3)
#define SEG_USER_DATA    0x23  // GDT 엔트리 4 | RPL=3 (0x20 | 3)
#define SEG_TSS          0x28  // GDT 엔트리 5

// ─────────────────────────────────────────────────────────────────────────────
// 유저 프로세스 가상 주소 공간 레이아웃
//
//  0x00000000 - 0x00000FFF  : NULL 가드 페이지
//  0x00001000 - 0x003FFFFF  : (예약)
//  0x00400000 - 0x7FFFFFFF  : 유저 코드/데이터/힙
//  0x80000000 - 0xBFFFFFFF  : 유저 스택 (하향 성장)
//  0xC0000000 - 0xFFFFFFFF  : 커널 공간 (유저 접근 불가)
// ─────────────────────────────────────────────────────────────────────────────
#define USER_CODE_BASE   0x00400000UL  // 유저 코드 로드 기저 주소
#define USER_STACK_TOP   0xBFFFF000UL  // 유저 스택 초기 ESP
#define USER_STACK_SIZE  (64 * 1024)   // 유저 스택 크기 (64KB)

// ─────────────────────────────────────────────────────────────────────────────
// Linux 호환 시스템 콜 번호 (int 0x80)
// ─────────────────────────────────────────────────────────────────────────────
#define SYS_EXIT         1
#define SYS_WRITE        4
#define SYS_READ         3
#define SYS_OPEN         5
#define SYS_CLOSE        6
#define SYS_GETPID       20
#define SYS_YIELD        158  // sched_yield

// ─────────────────────────────────────────────────────────────────────────────
// 유저 프로세스 디스크립터
// ─────────────────────────────────────────────────────────────────────────────
typedef struct {
    uint32_t  pid;               // 프로세스 ID
    char      name[32];          // 프로세스 이름
    uint32_t  page_dir;          // 페이지 디렉터리 물리 주소 (CR3)
    uint32_t  entry_point;       // ELF e_entry (가상 주소)
    uint32_t  user_stack_vaddr;  // 유저 스택 가상 주소 (top)
} user_proc_t;

// ─────────────────────────────────────────────────────────────────────────────
// 시스템 콜 핸들러 등록 (idt.c에서 int 0x80 벡터에 등록)
// ─────────────────────────────────────────────────────────────────────────────
void syscall_init(void);

/**
 * 시스템 콜 디스패처 (어셈블리 int 0x80 핸들러에서 호출)
 *
 * @param eax  시스템 콜 번호
 * @param ebx  인자 1
 * @param ecx  인자 2
 * @param edx  인자 3
 * @return     시스템 콜 반환값 (EAX에 저장됨)
 */
uint32_t syscall_dispatch(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx);

#endif // PARINOS_USER_H
