#include "shell.h"

#include <stdio.h>

#include "../std/string.h"
#include "../vga.h"
#include "../mem/pmm.h"
#include "../std/malloc.h"
#include "../drivers/timer.h"

// 🌟 1. 배열 대신 포인터로 변경 (kmalloc을 받기 위함)
char* cmd_buf = NULL;
int cmd_idx = 0;

// 셸 초기 상태 출력
void shell_init() {
    // 🌟 2. 최초 실행 시에만 버퍼를 동적 할당 (128바이트)
    if (cmd_buf == NULL) {
        cmd_buf = (char*)kmalloc(128);
    }

    // 🌟 3. 인덱스와 버퍼를 확실하게 초기화 (쓰레기값 방지)
    cmd_idx = 0;
    if (cmd_buf != NULL) {
        for(int i = 0; i < 128; i++) {
            cmd_buf[i] = '\0';
        }
    }

    kprintf("\n\nParinOS Shell v1.0 (March 2026)\n");
    kprintf("Type 'help' for a list of commands.\n");
    kprintf("\nParinOS> ");
}

// 셸 입력 처리 핵심 로직
void shell_input(char c) {
    // 방어 코드: 버퍼 할당 실패 시 작동 중지
    if (cmd_buf == NULL) return;

    if (c == '\n') {
        kputchar('\n');
        cmd_buf[cmd_idx] = '\0'; // 문자열 끝 널 문자 삽입

        // 1. md (Memory Dump) 명령어
        if (strncmp(cmd_buf, "md ", 3) == 0) {
            uint32_t addr = atoi_hex(cmd_buf + 3);
            dump_memory(addr, 8);
        }
        // 🌟 2. help 명령어 (복붙 실수 수정!)
        else if (strcmp(cmd_buf, "help") == 0) {
            kprintf("\nAvailable commands: help, md, clear, shell, test_malloc");
        }
        // 3. clear 명령어
        else if (strcmp(cmd_buf, "clear") == 0) {
            vga_clear();
        }
        // 4. shell 명령어
        else if (strcmp(cmd_buf, "shell") == 0) {
            shell_init();
        }
        // 5. test_malloc 명령어
        else if (strcmp(cmd_buf, "test_malloc") == 0) {
            kprintf("\n");
            char* str = (char*)kmalloc(32);
            const char* msg = "Hello from kmalloc!";
            int i = 0;
            while(msg[i]) { str[i] = msg[i]; i++; }
            str[i] = '\0';

            kprintf("Allocated Addr: %x\n", (uint32_t)str);
            kprintf("Data: %s\n", str);
            kfree(str);
            kprintf("Memory Freed.");
        } else if (strcmp(cmd_buf, "panic") == 0) {
            kprintf("\nCrashing system intentionally...\n");
            // 0x1000000(16MB)은 우리가 매핑한 마지막 지점입니다.
            // 그보다 살짝 높은 0x1100000을 건드려 봅시다.
            volatile uint32_t *bad_ptr = (uint32_t*)0x4000000;
            *bad_ptr = 0x1234;
            kprintf("If you see this, panic failed!\n");
        } else if (strncmp(cmd_buf, "delayd_echo ", 12) == 0) {
            // "delayd_echo Hello" -> "Hello" 부분만 추출
            char* message = cmd_buf + 12;

            kprintf("Preparing to echo in 3 seconds...\n");

            // 1. 카운트다운 효과 (XP 감성)
            for (int i = 3; i > 0; i--) {
                kprintf("%d... ", i);
                sleep(1000); // 1초 대기
            }
            kprintf("GO!\n\n");

            // 2. 타이핑 효과 (한 글자씩 출력)
            for (int i = 0; message[i] != '\0'; i++) {
                kputchar(message[i]);
                sleep(100); // 0.1초마다 한 글자씩 (타이핑 느낌)
            }
            kputchar('\n');
        } else if (strncmp(cmd_buf, "echo ", 5) == 0) {
            char* message = cmd_buf + 5;
            kprintf("%s\n", message);
        } else if (cmd_idx > 0) {
            kprintf("\nUnknown command: %s", cmd_buf);
        }

        // 프롬프트 재출력
        cmd_idx = 0;
        kprint("\nParinOS> ");

    } else if (c == '\b') {
        // 백스페이스 처리
        if (cmd_idx > 0) {
            cmd_idx--;
            kputchar('\b');
        }
    } else if (cmd_idx < 127) {
        // 일반 문자 입력
        cmd_buf[cmd_idx++] = c;
        kputchar(c);
    }
}