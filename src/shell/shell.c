#include "shell.h"
#include "../std/string.h"
#include "../vga.h"
#include "../mem/pmm.h"
#include "../std/malloc.h"
#include "../drivers/timer.h"
#include "../drivers/rtc.h"
#include "../mem/mem.h"

char* cmd_buf = NULL;
int cmd_idx = 0;

// --- 명령어 처리 함수들 ---

static void cmd_help() {
    kprintf("\nAvailable commands: help, md, clear, date, time, free, uptime, panic, echo");
}

static void cmd_clear() {
    vga_clear();
}

static void cmd_md() {
    // "md 0x800000" 형태에서 주소 부분 추출
    uint32_t addr = atoi_hex(cmd_buf + 3);
    dump_memory(addr, 8);
}

static void cmd_date() {
    rtc_time_t now; // 굳이 kmalloc 하지 않고 스택 사용
    read_rtc(&now);
    kprintf("%s %d, %d\n", get_month_name(now.month), now.day, now.year);
}

static void cmd_time() {
    rtc_time_t now;
    read_rtc(&now);
    kprintf("%d:%d:%d\n", now.hour, now.minute, now.second);
}

static void cmd_free() {
    // malloc.c에 구현할 함수 호출
    dump_heap_stat();
}

static void cmd_uptime() {
    uint32_t t = get_total_ticks(); // timer.c에서 구현된 틱 값
    uint32_t sec = t / 1000;
    uint32_t min = sec / 60;
    uint32_t hour = min / 60;
    kprintf("Uptime: %d hours, %d minutes, %d seconds\n", hour, min % 60, sec % 60);
}

static void cmd_panic() {
    kprintf("\nCrashing system intentionally...\n");
    volatile uint32_t *bad_ptr = (uint32_t*)0x4000000;
    *bad_ptr = 0x1234;
}

static void cmd_cpuinfo() {
    uint32_t eax, ebx, ecx, edx;
    char vendor[13];

    // CPU 제조사 문자열 가져오기 (EAX=0)
    asm volatile("cpuid" : "=b"(ebx), "=d"(edx), "=c"(ecx) : "a"(0));

    // EBX, EDX, ECX 순서로 문자열이 들어옵니다.
    memcpy(vendor, &ebx, 4);
    memcpy(vendor + 4, &edx, 4);
    memcpy(vendor + 8, &ecx, 4);
    vendor[12] = '\0';

    kprintf("CPU Vendor: %s\n", vendor);
}

// --- 메인 셸 로직 ---

void shell_init() {
    if (cmd_buf == NULL) cmd_buf = (char*)kmalloc(128);
    cmd_idx = 0;
    memset(cmd_buf, 0, 128);

    kprintf("\nParinOS Kernel Shell\n");
    kprintf("ParinOS> ");
}

void process_command() {
    kputchar('\n');
    cmd_buf[cmd_idx] = '\0';

    if (cmd_idx == 0) return;

    // 🌟 이 부분을 함수별로 분리
    if (strcmp(cmd_buf, "help") == 0)          cmd_help();
    else if (strcmp(cmd_buf, "clear") == 0)   cmd_clear();
    else if (strncmp(cmd_buf, "md ", 3) == 0) cmd_md();
    else if (strcmp(cmd_buf, "date") == 0)    cmd_date();
    else if (strcmp(cmd_buf, "time") == 0)    cmd_time();
    else if (strcmp(cmd_buf, "free") == 0)    cmd_free();
    else if (strcmp(cmd_buf, "uptime") == 0)  cmd_uptime();
    else if (strcmp(cmd_buf, "panic") == 0)   cmd_panic();
    else if (strcmp(cmd_buf, "cpuinfo") == 0) cmd_cpuinfo();
    else if (strncmp(cmd_buf, "echo ", 5) == 0) {
        kprintf("%s\n", cmd_buf + 5);
    }
    else {
        kprintf("Unknown command: %s\n", cmd_buf);
    }

    cmd_idx = 0;
    memset(cmd_buf, 0, 128);
    kprintf("ParinOS> ");
}

void shell_input(char c) {
    if (cmd_buf == NULL) return;

    if (c == '\n') {
        process_command();
    } else if (c == '\b') {
        if (cmd_idx > 0) {
            cmd_idx--;
            kputchar('\b');
        }
    } else if (cmd_idx < 127) {
        cmd_buf[cmd_idx++] = c;
        kputchar(c);
    }
}