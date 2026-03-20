//
// src/user/user.c
// 유저 모드 시스템 콜 디스패처 및 기본 syscall 구현
//

#include "user.h"
#include "../hal/vga.h"
#include "../kernel/multitasking.h"

// ─────────────────────────────────────────────────────────────────────────────
// 개별 시스템 콜 구현
// ─────────────────────────────────────────────────────────────────────────────

// SYS_EXIT(1): 현재 프로세스 종료
static uint32_t sys_exit(uint32_t code) {
    klog_info("[SYSCALL] sys_exit(%d)\n", (int)code);
    kprocess_exit();
    // 실행 여기 도달하지 않음
    return 0;
}

// SYS_WRITE(4): 파일 디스크립터에 쓰기 (fd=1/2 → VGA 출력)
static uint32_t sys_write(uint32_t fd, uint32_t buf_vaddr, uint32_t count) {
    if (fd == 1 || fd == 2) {
        // stdout / stderr → VGA에 출력
        const char* s = (const char*)buf_vaddr;
        for (uint32_t i = 0; i < count; i++) {
            lkputchar(s[i]);
        }
        return count;
    }
    return (uint32_t)-1; // EBADF
}

// SYS_GETPID(20): 현재 프로세스 ID 반환
static uint32_t sys_getpid(void) {
    return (uint32_t)kprocess_id();
}

// SYS_YIELD(158): CPU 양보
static uint32_t sys_yield(void) {
    kschedule();
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// 시스템 콜 디스패처
// ─────────────────────────────────────────────────────────────────────────────
uint32_t syscall_dispatch(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    switch (eax) {
        case SYS_EXIT:   return sys_exit(ebx);
        case SYS_WRITE:  return sys_write(ebx, ecx, edx);
        case SYS_GETPID: return sys_getpid();
        case SYS_YIELD:  return sys_yield();
        default:
            klog_warn("[SYSCALL] Unknown syscall: %d\n", (int)eax);
            return (uint32_t)-38; // -ENOSYS
    }
}

// syscall_init은 idt.c에서 int 0x80 핸들러를 등록한 뒤 호출됩니다.
void syscall_init(void) {
    klog_info("[SYSCALL] System call interface ready (int 0x80)\n");
}
