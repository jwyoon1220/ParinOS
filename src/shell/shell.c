#include "shell.h"
#include "../std/kstring.h"
#include "../hal/vga.h"
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
#include "../font/font.h"

// ─────────────────────────────────────────────────────────────────────────────
// 셸 상태
// ─────────────────────────────────────────────────────────────────────────────
char* cmd_buf = NULL;
int   cmd_idx = 0; // 커서 인덱스 (0 ~ cmd_len)
int   cmd_len = 0; // 버퍼에 담긴 글자의 총 길이

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
    kprintf("  help          - Show This message\n");
    kprintf("  clear         - Clear the screen\n");
    kprintf("  cd <path>     - Change Currend Work Directory\n");
    kprintf("  ls [path]     - list of files and dirsa\n");
    kprintf("  cat <path>    - Print the name of file\n");
    kprintf("  echo <text>   - echo\n");
    kprintf("  run <path> [args]  - run elf32 with optional arguments\n");
    kprintf("  ./<path> [args]    - run elf32 directly with optional arguments\n");
    kprintf("  playsound <p> - Play wav\n");
    kprintf("  md <addr>     - momry dump\n");
    kprintf("  date          - Date\n");
    kprintf("  time          - Time\n");
    kprintf("  free          - Heap Statistics\n");
    kprintf("  uptime        - Uptime\n");
    kprintf("  cpuinfo       - CPU Information\n");
    kprintf("  pci_info      - List of PCI Devices\n");
    kprintf("  vmm_stat      - VMM Statistics\n");
    kprintf("  task_view     - Processes and threads\n");
    kprintf("  fs            - File System Info\n");
    kprintf("  reboot        - Reboot\n");
    kprintf("  panic         - Panic\n");
    kprintf("  cng_font <p> [sz] - Load TrueType font from path (size optional, default 16)\n");
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

/* cng_font <path> [size]  – TrueType 폰트 변경 */
static void cmd_cng_font(const char* arg) {
    if (arg == NULL || *arg == '\0') {
        kprintf("Usage: cng_font <path> [size]\n");
        kprintf("  Example: cng_font /0/fonts/mono.ttf 16\n");
        return;
    }

    char path[256];
    int  size = 16;

    /* 공백으로 경로와 크기 분리 */
    const char *sp = strchr(arg, ' ');
    if (sp) {
        int plen = (int)(sp - arg);
        if (plen >= 256) plen = 255;
        int k;
        for (k = 0; k < plen; k++) path[k] = arg[k];
        path[plen] = '\0';

        /* 크기 파싱 */
        const char *sz = sp + 1;
        while (*sz == ' ') sz++;
        size = 0;
        while (*sz >= '0' && *sz <= '9') {
            size = size * 10 + (*sz - '0');
            sz++;
        }
        if (size <= 0) size = 16;
    } else {
        strcpy(path, arg);
    }

    char abs_path[256];
    resolve_path(path, abs_path, sizeof(abs_path));

    kprintf("Loading font: %s (size %d)\n", abs_path, size);
    if (font_load_ttf(abs_path, size) != 0) {
        klog_error("cng_font: Failed to load '%s'\n", abs_path);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 셸 초기화
// ─────────────────────────────────────────────────────────────────────────────
static void shell_redraw_line(void) {
    vga_draw_cursor(0);
    vga_clear_current_line();
    lkputchar('\r');
    print_prompt();

    int target_cx = vga_get_cx();
    for (int i = 0; i < cmd_len; i++) {
        if (i == cmd_idx) {
            target_cx = vga_get_cx();
        }
        char c = cmd_buf[i];
        if (c >= 32 || c == '\n' || c < 0) {
            lkputchar(c);
        }
    }
    if (cmd_idx == cmd_len) {
        target_cx = vga_get_cx();
    }
    
    vga_set_cx(target_cx);
    vga_draw_cursor(1);
}

// ─────────────────────────────────────────────────────────────────────────────
// 셸 초기화
// ─────────────────────────────────────────────────────────────────────────────
void shell_init() {
    if (cmd_buf == NULL) cmd_buf = (char*)kmalloc(128);
    cmd_idx = 0;
    cmd_len = 0;
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

    shell_redraw_line();
}

// ─────────────────────────────────────────────────────────────────────────────
// 명령어 파싱 및 디스패치
// ─────────────────────────────────────────────────────────────────────────────
void process_command() {
    lkputchar('\n');
    cmd_buf[cmd_len] = '\0';

    if (cmd_len == 0) {
        shell_redraw_line();
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

    // ── run <path> [arg1 arg2 ...] ──────────────────────────────────────────
    else if (strncmp(cmd_buf, "run ", 4) == 0) {
        char* target_path = cmd_buf + 4;
        while (*target_path == ' ') target_path++;
        trim_spaces(target_path);

        if (strlen(target_path) > 0) {
            /* 경로와 인수 분리 */
            char  abs_path[256];
            char  arg_buf[256];
            const char *argv[32];
            int   argc = 0;

            /* 첫 번째 토큰 = 경로 */
            char *sp = target_path;
            int   pi = 0;
            while (*sp && *sp != ' ' && pi < 254) arg_buf[pi++] = *sp++;
            arg_buf[pi] = '\0';
            resolve_path(arg_buf, abs_path, sizeof(abs_path));
            argv[argc++] = abs_path;

            /* 나머지 토큰 = 인수 */
            while (*sp && argc < 31) {
                while (*sp == ' ') sp++;
                if (!*sp) break;
                argv[argc++] = sp;
                while (*sp && *sp != ' ') sp++;
                if (*sp) { *sp = '\0'; sp++; }
            }
            argv[argc] = 0;

            kprintf("Loading '%s' (argc=%d)...\n", abs_path, argc);
            if (elf_execute_with_args(abs_path, argc, argv) == RUN_SUCCESS) {
                kprintf("\nProgram '%s' finished successfully.\n", abs_path);
            } else {
                kprintf("\nFailed to execute '%s'.\n", abs_path);
            }
        } else {
            kprintf("Usage: run <filepath> [args...]\n");
        }
    }

    // ── ./path [arg1 arg2 ...] 직접 실행 ────────────────────────────────────
    else if (cmd_buf[0] == '.' && cmd_buf[1] == '/') {
        char  abs_path[256];
        char  arg_buf[256];
        const char *argv[32];
        int   argc = 0;

        char *sp = cmd_buf;
        int   pi = 0;
        while (*sp && *sp != ' ' && pi < 254) arg_buf[pi++] = *sp++;
        arg_buf[pi] = '\0';
        resolve_path(arg_buf, abs_path, sizeof(abs_path));
        argv[argc++] = abs_path;

        while (*sp && argc < 31) {
            while (*sp == ' ') sp++;
            if (!*sp) break;
            argv[argc++] = sp;
            while (*sp && *sp != ' ') sp++;
            if (*sp) { *sp = '\0'; sp++; }
        }
        argv[argc] = 0;

        kprintf("Loading '%s' (argc=%d)...\n", abs_path, argc);
        if (elf_execute_with_args(abs_path, argc, argv) == RUN_SUCCESS) {
            kprintf("\nProgram '%s' finished successfully.\n", abs_path);
        } else {
            kprintf("\nFailed to execute '%s'.\n", abs_path);
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

    // ── cng_font [path] [size] ────────────────────────────────────────────────
    else if (strcmp(cmd_buf, "cng_font") == 0) {
        cmd_cng_font(NULL);
    }
    else if (strncmp(cmd_buf, "cng_font ", 9) == 0) {
        char* arg = cmd_buf + 9;
        while (*arg == ' ') arg++;
        trim_spaces(arg);
        cmd_cng_font(arg);
    }

    // ── 알 수 없는 명령어 ─────────────────────────────────────────────────────
    else {
        klog_warn("Unknown command: '%s' (type 'help' for list)\n", cmd_buf);
    }

    cmd_idx = 0;
    cmd_len = 0;
    memset(cmd_buf, 0, 128);
    shell_redraw_line();
}

// ─────────────────────────────────────────────────────────────────────────────
// 키 입력 처리
// ─────────────────────────────────────────────────────────────────────────────
void shell_input(char c) {
    if (cmd_buf == NULL) return;

    if (c == '\n') {
        cmd_buf[cmd_len] = '\0';
        vga_draw_cursor(0);
        vga_clear_current_line();
        lkputchar('\r');
        print_prompt();
        for (int i = 0; i < cmd_len; i++) {
            char oc = cmd_buf[i];
            if (oc >= 32 || oc == '\n' || oc < 0) lkputchar(oc);
        }
        process_command();
    } else if (c == 17) { // Left arrow
        if (cmd_idx > 0) {
            cmd_idx--;
            while (cmd_idx > 0 && (cmd_buf[cmd_idx] & 0xC0) == 0x80) {
                cmd_idx--;
            }
            shell_redraw_line();
        }
    } else if (c == 18) { // Right arrow
        if (cmd_idx < cmd_len) {
            cmd_idx++;
            while (cmd_idx < cmd_len && (cmd_buf[cmd_idx] & 0xC0) == 0x80) {
                cmd_idx++;
            }
            shell_redraw_line();
        }
    } else if (c == '\b') {
        if (cmd_idx > 0) {
            int start_idx = cmd_idx - 1;
            while (start_idx > 0 && (cmd_buf[start_idx] & 0xC0) == 0x80) {
                start_idx--;
            }
            int deleted_bytes = cmd_idx - start_idx;
            for (int i = cmd_idx; i < cmd_len; i++) {
                cmd_buf[i - deleted_bytes] = cmd_buf[i];
            }
            cmd_len -= deleted_bytes;
            cmd_idx = start_idx;
            shell_redraw_line();
        }
    } else if (cmd_len < 127) {
        for (int i = cmd_len; i > cmd_idx; i--) {
            cmd_buf[i] = cmd_buf[i - 1];
        }
        cmd_buf[cmd_idx] = c;
        cmd_idx++;
        cmd_len++;
        shell_redraw_line();
    }
}
