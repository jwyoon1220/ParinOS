#include "shell.h"
#include "../std/string.h"
#include "../vga.h"
#include "../mem/pmm.h"
#include "../std/malloc.h"
#include "../drivers/timer.h"
#include "../drivers/rtc.h"
#include "../mem/mem.h"
#include "../drivers/pci.h"
#include "../fs/fs.h"
#include "../util/util.h"
#include "../kernel/multitasking.h"
#include "../mem/vmm.h"
#include "../elf/elf.h"
#include "../std/kstd.h"
#include "../drivers/speaker.h"

// ─────────────────────────────────────────────────────────────────────────────
// 셸 상태
// ─────────────────────────────────────────────────────────────────────────────
char* cmd_buf = NULL;
int   cmd_idx = 0;

// 현재 작업 디렉터리 (FAT32 경로 형식: "/0/subdir/")
static char cwd[256];

// ─────────────────────────────────────────────────────────────────────────────
// 내부 헬퍼
// ─────────────────────────────────────────────────────────────────────────────

static void trim_spaces(char* str) {
    char* end;
    end = str + strlen(str) - 1;
    while (end > str && *end == ' ') end--;
    *(end + 1) = '\0';
}

/** 상대 경로를 cwd 기준으로 절대 경로로 변환.
 *  결과는 dst(최대 dst_size)에 저장됩니다.
 *  이미 '/' 로 시작하면 절대 경로로 그대로 씁니다. */
static void resolve_path(const char* rel, char* dst, int dst_size) {
    if (rel[0] == '/') {
        // 절대 경로
        int i;
        for (i = 0; rel[i] && i < dst_size - 1; i++) dst[i] = rel[i];
        dst[i] = '\0';
        return;
    }

    // "cd .." 처리: cwd 에서 마지막 디렉터리 하나 제거
    if (rel[0] == '.' && rel[1] == '.' && (rel[2] == '\0' || rel[2] == '/')) {
        // dst = cwd 에서 마지막 '/' 이전까지
        int len = strlen(cwd);
        // 끝 '/' 제거 (존재할 경우)
        if (len > 1 && cwd[len - 1] == '/') len--;
        // 이전 '/' 찾기
        while (len > 0 && cwd[len - 1] != '/') len--;
        if (len == 0) len = 1; // 루트 보호
        int i;
        for (i = 0; i < len && i < dst_size - 1; i++) dst[i] = cwd[i];
        dst[i] = '\0';
        return;
    }

    // 상대 경로: cwd + rel 결합
    int cwdlen = strlen(cwd);
    int i;
    for (i = 0; i < cwdlen && i < dst_size - 2; i++) dst[i] = cwd[i];
    // cwd 끝에 '/' 보장
    if (i > 0 && dst[i - 1] != '/') dst[i++] = '/';
    // rel 추가
    for (int j = 0; rel[j] && i < dst_size - 1; j++, i++) dst[i] = rel[j];
    dst[i] = '\0';
}

// ─────────────────────────────────────────────────────────────────────────────
// 프롬프트 출력 (현재 경로 포함)
// ─────────────────────────────────────────────────────────────────────────────
static void print_prompt(void) {
    vga_set_color(VGA_COLOR_INFO);
    kprintf("ParinOS");
    vga_set_color(VGA_COLOR_DEFAULT);
    kprintf(":");
    vga_set_color(VGA_COLOR(VGA_FG_LIGHT_CYAN, VGA_BG_BLACK));
    kprintf("%s", cwd);
    vga_set_color(VGA_COLOR_DEFAULT);
    kprintf("$ ");
}

// ─────────────────────────────────────────────────────────────────────────────
// 명령어 구현
// ─────────────────────────────────────────────────────────────────────────────

static void cmd_help() {
    kprintf("\nAvailable commands:\n");
    kprintf("  help          - 도움말 출력\n");
    kprintf("  clear         - 화면 지우기\n");
    kprintf("  cd <path>     - 작업 디렉터리 변경\n");
    kprintf("  ls [path]     - 디렉터리 목록\n");
    kprintf("  cat <path>    - 파일 출력 (> 리디렉션 지원)\n");
    kprintf("  echo <text>   - 텍스트 출력 (> 리디렉션 지원)\n");
    kprintf("  run <path>    - ELF 실행\n");
    kprintf("  playsound <p> - WAV 파일 재생\n");
    kprintf("  md <addr>     - 메모리 덤프 (16진수 주소)\n");
    kprintf("  date          - 현재 날짜\n");
    kprintf("  time          - 현재 시각\n");
    kprintf("  free          - 힙 통계\n");
    kprintf("  uptime        - 가동 시간\n");
    kprintf("  cpuinfo       - CPU 정보\n");
    kprintf("  pci_info      - PCI 장치 목록\n");
    kprintf("  vmm_stat      - 가상 메모리 통계\n");
    kprintf("  task_view     - 프로세스/스레드 목록\n");
    kprintf("  fs            - 파일 시스템 정보\n");
    kprintf("  reboot        - 시스템 재시작\n");
    kprintf("  panic         - 강제 패닉 (테스트용)\n");
}

static void cmd_clear() {
    vga_clear();
}

static void cmd_cd(const char* arg) {
    if (arg == NULL || *arg == '\0') {
        // 인수 없으면 루트로 이동
        strcpy(cwd, "/0/");
        return;
    }

    char new_path[256];
    resolve_path(arg, new_path, sizeof(new_path));

    // 끝에 '/' 추가 (없으면)
    int len = strlen(new_path);
    if (len > 0 && new_path[len - 1] != '/') {
        new_path[len]     = '/';
        new_path[len + 1] = '\0';
    }

    // 경로 존재 확인 (디렉터리 열기 시도)
    Dir dir;
    int err = fat_dir_open(&dir, new_path);
    if (err != FAT_ERR_NONE) {
        klog_error("cd: '%s': No such directory\n", new_path);
        return;
    }

    strcpy(cwd, new_path);
}

static void cmd_md() {
    uint32_t addr = atoi_hex(cmd_buf + 3);
    dump_memory(addr, 8);
}

static void cmd_date() {
    rtc_time_t now;
    read_rtc(&now);
    kprintf("%s %d, %d\n", get_month_name(now.month), now.day, now.year);
}

static void cmd_time() {
    rtc_time_t now;
    read_rtc(&now);
    kprintf("%02d:%02d:%02d\n", now.hour, now.minute, now.second);
}

static void cmd_free() {
    dump_heap_stat();
}

static void cmd_uptime() {
    uint32_t t = get_total_ticks();
    uint32_t sec = t / 1000;
    uint32_t min = sec / 60;
    uint32_t hour = min / 60;
    kprintf("Uptime: %d hours, %d minutes, %d seconds\n", hour, min % 60, sec % 60);
}

static void cmd_panic() {
    kprintf("\nCrashing system intentionally...\n");
    volatile uint32_t *bad_ptr = (uint32_t*)0x0;
    *bad_ptr = 0x1234;
}

static void cmd_cpuinfo() {
    uint32_t eax, ebx, ecx, edx;
    char vendor[13];

    asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=d"(edx), "=c"(ecx) : "a"(0));

    memcpy(vendor, &ebx, 4);
    memcpy(vendor + 4, &edx, 4);
    memcpy(vendor + 8, &ecx, 4);
    vendor[12] = '\0';

    kprintf("CPU Vendor: %s\n", vendor);
}

static void cmd_ls(const char* arg) {
    char path[256];
    if (arg == NULL || *arg == '\0') {
        // 인수 없으면 현재 디렉터리 사용
        strcpy(path, cwd);
    } else {
        resolve_path(arg, path, sizeof(path));
        // 디렉터리 경로 끝에 '/' 보장
        int len = strlen(path);
        if (len > 0 && path[len - 1] != '/') {
            path[len]     = '/';
            path[len + 1] = '\0';
        }
    }
    fs_ls(path);
}

static void cmd_playsound(const char* arg) {
    if (arg == NULL || *arg == '\0') {
        kprintf("Usage: playsound <filepath>\n");
        kprintf("  예: playsound /0/sound.wav\n");
        return;
    }

    char path[256];
    resolve_path(arg, path, sizeof(path));

    klog_info("Playing: %s\n", path);
    if (!speaker_play_wav(path)) {
        klog_error("playsound: Failed to play '%s'\n", path);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 셸 초기화
// ─────────────────────────────────────────────────────────────────────────────
void shell_init() {
    if (cmd_buf == NULL) cmd_buf = (char*)kmalloc(128);
    cmd_idx = 0;
    memset(cmd_buf, 0, 128);

    // 작업 디렉터리 초기화
    strcpy(cwd, "/0/");

    // 스피커 초기화
    speaker_init();

    kprintf("\n");
    vga_set_color(VGA_COLOR_INFO);
    kprintf("  ____           _       ___  ____  \n");
    kprintf(" |  _ \\ __ _ _ __(_)_ __ / _ \\/ ___| \n");
    kprintf(" | |_) / _` | '__| | '_ \\| | | \\___ \\ \n");
    kprintf(" |  __/ (_| | |  | | | | | |_| |___) |\n");
    kprintf(" |_|   \\__,_|_|  |_|_| |_|\\___/|____/ \n");
    vga_set_color(VGA_COLOR_DEFAULT);
    kprintf("\nParinOS Kernel Shell  (type 'help' for commands)\n\n");

    print_prompt();
}

// ─────────────────────────────────────────────────────────────────────────────
// 명령어 파싱 및 디스패치
// ─────────────────────────────────────────────────────────────────────────────
void process_command() {
    lkputchar('\n');
    cmd_buf[cmd_idx] = '\0';

    if (cmd_idx == 0) {
        print_prompt();
        return;
    }

    // ── 단순 명령어 ──────────────────────────────────────────────────────────
    if (strcmp(cmd_buf, "help") == 0)        cmd_help();
    else if (strcmp(cmd_buf, "clear") == 0)  cmd_clear();
    else if (strcmp(cmd_buf, "date") == 0)   cmd_date();
    else if (strcmp(cmd_buf, "time") == 0)   cmd_time();
    else if (strcmp(cmd_buf, "free") == 0)   cmd_free();
    else if (strcmp(cmd_buf, "uptime") == 0) cmd_uptime();
    else if (strcmp(cmd_buf, "panic") == 0)  cmd_panic();
    else if (strcmp(cmd_buf, "cpuinfo") == 0) cmd_cpuinfo();
    else if (strcmp(cmd_buf, "pci_info") == 0) pci_list_devices();
    else if (strcmp(cmd_buf, "task_view") == 0) dump_multitasking_info();
    else if (strcmp(cmd_buf, "vmm_stat") == 0) vmm_print_stats();
    else if (strcmp(cmd_buf, "fs") == 0)     fs_print_info();

    // ── md <addr> ────────────────────────────────────────────────────────────
    else if (strncmp(cmd_buf, "md ", 3) == 0) cmd_md();

    // ── cd [path] ────────────────────────────────────────────────────────────
    else if (strcmp(cmd_buf, "cd") == 0) {
        cmd_cd(NULL);
    }
    else if (strncmp(cmd_buf, "cd ", 3) == 0) {
        char* arg = cmd_buf + 3;
        while (*arg == ' ') arg++;
        trim_spaces(arg);
        cmd_cd(arg);
    }

    // ── ls [path] ────────────────────────────────────────────────────────────
    else if (strcmp(cmd_buf, "ls") == 0) {
        cmd_ls(NULL);
    }
    else if (strncmp(cmd_buf, "ls ", 3) == 0) {
        char* arg = cmd_buf + 3;
        while (*arg == ' ') arg++;
        trim_spaces(arg);
        cmd_ls(arg);
    }

    // ── playsound <path> ─────────────────────────────────────────────────────
    else if (strncmp(cmd_buf, "playsound ", 10) == 0) {
        char* arg = cmd_buf + 10;
        while (*arg == ' ') arg++;
        trim_spaces(arg);
        cmd_playsound(arg);
    }

    // ── run <path> ───────────────────────────────────────────────────────────
    else if (strncmp(cmd_buf, "run ", 4) == 0) {
        char* target_path = cmd_buf + 4;
        while (*target_path == ' ') target_path++;
        trim_spaces(target_path);

        if (strlen(target_path) > 0) {
            char abs_path[256];
            resolve_path(target_path, abs_path, sizeof(abs_path));
            kprintf("Loading '%s'...\n", abs_path);
            if (elf_execute_from_path(abs_path) == RUN_SUCCESS) {
                kprintf("\nProgram '%s' finished successfully.\n", abs_path);
            } else {
                kprintf("\nFailed to execute '%s'.\n", abs_path);
            }
        } else {
            kprintf("Usage: run <filepath>\n");
        }
    }

    // ── cat <path> [> dest] ──────────────────────────────────────────────────
    else if (strncmp(cmd_buf, "cat ", 4) == 0) {
        char* redir_ptr = strchr(cmd_buf, '>');

        if (redir_ptr != NULL) {
            *redir_ptr = '\0';
            char* src_cmd   = cmd_buf;
            char* dest_path = redir_ptr + 1;
            while (*dest_path == ' ') dest_path++;
            trim_spaces(src_cmd);
            trim_spaces(dest_path);

            if (strncmp(src_cmd, "cat ", 4) == 0) {
                char* src_rel = src_cmd + 4;
                while (*src_rel == ' ') src_rel++;
                char abs_src[256], abs_dst[256];
                resolve_path(src_rel,   abs_src, sizeof(abs_src));
                resolve_path(dest_path, abs_dst, sizeof(abs_dst));
                fs_redirect_to_file(abs_src, abs_dst);
            }
        } else {
            char* src_rel = cmd_buf + 4;
            while (*src_rel == ' ') src_rel++;
            trim_spaces(src_rel);
            char abs_path[256];
            resolve_path(src_rel, abs_path, sizeof(abs_path));
            fs_cat(abs_path);
        }
    }

    // ── echo <text> [> dest] ─────────────────────────────────────────────────
    else if (strncmp(cmd_buf, "echo ", 5) == 0) {
        char* content   = cmd_buf + 5;
        char* redir_ptr = strchr(content, '>');

        if (redir_ptr != NULL) {
            *redir_ptr = '\0';
            char* dest_path = redir_ptr + 1;
            trim_spaces(content);
            while (*dest_path == ' ') dest_path++;
            trim_spaces(dest_path);
            char abs_dst[256];
            resolve_path(dest_path, abs_dst, sizeof(abs_dst));
            fs_write_string(abs_dst, content);
        } else {
            kprintf("%s\n", content);
        }
    }

    // ── reboot ───────────────────────────────────────────────────────────────
    else if (strcmp(cmd_buf, "reboot") == 0) {
        kprintf("Rebooting system...\n");
        sleep(1000);
        asm volatile("int $0x19");
    }

    // ── 알 수 없는 명령어 ─────────────────────────────────────────────────────
    else {
        klog_warn("Unknown command: '%s' (type 'help' for list)\n", cmd_buf);
    }

    cmd_idx = 0;
    memset(cmd_buf, 0, 128);
    print_prompt();
}

// ─────────────────────────────────────────────────────────────────────────────
// 키 입력 처리
// ─────────────────────────────────────────────────────────────────────────────
void shell_input(char c) {
    if (cmd_buf == NULL) return;

    if (c == '\n') {
        process_command();
    } else if (c == '\b') {
        if (cmd_idx > 0) {
            cmd_idx--;
            lkputchar('\b');
        }
    } else if (cmd_idx < 127) {
        cmd_buf[cmd_idx++] = c;
        lkputchar(c);
    }
}
