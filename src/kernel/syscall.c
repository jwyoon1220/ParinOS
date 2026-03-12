#include "syscall.h"
#include "multitasking.h"
#include "../vga.h"
#include "../mem/vmm.h"

// 유저 포인터 유효성 검사: 유저 주소 범위 내에 있는지 확인
static int is_valid_user_ptr(uint32_t ptr, uint32_t len) {
    if (ptr == 0) return 0;
    if (!vmm_is_user_address(ptr)) return 0;
    // 길이가 합리적인 범위인지 확인 (최대 4KB)
    if (len > 4096) return 0;
    // 포인터 + 길이가 유저 주소 공간을 벗어나지 않는지 확인
    if (ptr + len < ptr) return 0;  // 오버플로우 방지
    return 1;
}

uint32_t do_syscall(uint32_t num, uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    (void)arg3;  // 현재 사용하지 않는 3번째 인자

    switch (num) {

    case SYS_EXIT:
        // 현재 스레드를 종료합니다. 스케줄러가 다른 스레드로 전환합니다.
        kthread_exit();
        // kthread_exit()는 반환하지 않음 (무한 hlt 루프)
        return 0;

    case SYS_WRITE: {
        // arg1: 문자열 포인터 (유저 가상 주소), arg2: 길이
        uint32_t ptr = arg1;
        uint32_t len = arg2;

        // 유저 포인터 유효성 검사
        if (!is_valid_user_ptr(ptr, len)) {
            return (uint32_t)-1;
        }

        const char *str = (const char *)ptr;
        uint32_t i;
        for (i = 0; i < len && str[i] != '\0'; i++) {
            lkputchar(str[i]);
        }
        return i;
    }

    case SYS_GETPID:
        return (uint32_t)kprocess_id();

    default:
        // 알 수 없는 시스템 콜: -1 반환
        return (uint32_t)-1;
    }

    return 0;
}
