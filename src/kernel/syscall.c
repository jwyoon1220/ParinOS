#include "syscall.h"
#include "multitasking.h"
#include "../vga.h"

uint32_t do_syscall(uint32_t num, uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    switch (num) {

    case SYS_EXIT:
        // 현재 스레드를 종료합니다. 스케줄러가 다른 스레드로 전환합니다.
        kthread_exit();
        // kthread_exit()는 반환하지 않음 (무한 hlt 루프)
        return 0;

    case SYS_WRITE: {
        // arg1: 문자열 포인터 (유저 가상 주소), arg2: 길이
        const char *str = (const char *)arg1;
        uint32_t len = arg2;
        // 커널은 유저 페이지에 직접 접근 가능 (Ring 0)
        for (uint32_t i = 0; i < len && str[i] != '\0'; i++) {
            lkputchar(str[i]);
        }
        return len;
    }

    case SYS_GETPID:
        return (uint32_t)kprocess_id();

    default:
        // 알 수 없는 시스템 콜: -1 반환
        return (uint32_t)-1;
    }

    // 도달하지 않음
    (void)arg3;
    return 0;
}
