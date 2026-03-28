/*
 * crt0.c — C 런타임 스타트업 (유저 프로그램용)
 *
 * elf_execute_with_args 가 _start(argc, argv) 를 직접 호출합니다.
 * _start 는 main 을 호출하고, 반환 후 SYS_EXIT 로 종료합니다.
 */

#include "syscall.h"

extern int main(int argc, const char **argv);

void _start(int argc, const char **argv) {
    int ret = main(argc, argv);
    syscall1(SYS_EXIT, ret);
    /* 도달하지 않음 */
    while (1) {}
}
