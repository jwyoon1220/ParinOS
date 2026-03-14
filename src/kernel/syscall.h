#ifndef PARINOS_SYSCALL_H
#define PARINOS_SYSCALL_H

#include <stdint.h>

// ─── 시스템 콜 번호 ────────────────────────────────────────────────────────
// 유저 프로그램은 int 0x80 호출 시 EAX에 아래 번호를 담아야 합니다.
// 인자는 EBX(arg1), ECX(arg2), EDX(arg3)이며 반환값은 EAX입니다.

#define SYS_EXIT    0   // 현재 스레드 종료 (EBX = 종료 코드)
#define SYS_WRITE   1   // 콘솔 출력 (EBX = 문자열 포인터, ECX = 길이)
#define SYS_GETPID  2   // 현재 프로세스 ID 반환

// ─── 시스템 콜 디스패처 ────────────────────────────────────────────────────
// syscall_stub (idt_asm.asm)에서 호출됩니다.
// 반환값은 EAX를 통해 유저 프로그램으로 전달됩니다.
uint32_t do_syscall(uint32_t num, uint32_t arg1, uint32_t arg2, uint32_t arg3);

#endif // PARINOS_SYSCALL_H
