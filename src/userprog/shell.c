/*
 * shell.c — ParinOS 유저 모드 셸 (src/userprog/shell.c)
 *
 * 커널 셸(src/shell/shell.c)과 독립적으로 동작하는 유저 프로그램 셸입니다.
 * int 0x80 시스템 콜을 통해서만 커널과 통신합니다.
 *
 * 실행: run /bin/shell  또는  /bin/shell
 */

#include "include/stdio.h"
#include "include/stdlib.h"
#include "include/string.h"
#include "include/unistd.h"

#define CMD_MAX   256
#define ARGV_MAX   32

/* ── 현재 작업 디렉터리 ─────────────────────────────────────────────── */
static char g_cwd[256] = "/";

/* ── 프롬프트 출력 ───────────────────────────────────────────────────── */
static void print_prompt(void) {
    printf("\033[32mParinOS\033[0m:\033[36m%s\033[0m$ ", g_cwd);
}

/* ── 문자열 앞뒤 공백 제거 ────────────────────────────────────────── */
static void trim(char *s) {
    int len = (int)strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\n' || s[len-1] == '\r'))
        s[--len] = '\0';
}

/* ── 명령줄 파싱 → argc/argv ─────────────────────────────────────── */
static int parse_args(char *line, const char **argv, int max_argc) {
    int argc = 0;
    char *p = line;
    while (*p && argc < max_argc - 1) {
        while (*p == ' ') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = '\0';
    }
    argv[argc] = (const char*)0;
    return argc;
}

/* ── 절대 경로 변환 ──────────────────────────────────────────────── */
static void resolve_path(const char *rel, char *dst, int dst_size) {
    if (rel[0] == '/') {
        strncpy(dst, rel, (size_t)(dst_size - 1));
        dst[dst_size - 1] = '\0';
        return;
    }
    /* ".." 처리 */
    if (rel[0] == '.' && rel[1] == '.' && (rel[2] == '\0' || rel[2] == '/')) {
        int len = (int)strlen(g_cwd);
        if (len > 1 && g_cwd[len-1] == '/') len--;
        while (len > 0 && g_cwd[len-1] != '/') len--;
        if (len == 0) len = 1;
        strncpy(dst, g_cwd, (size_t)len);
        dst[len] = '\0';
        return;
    }
    /* 상대 경로: cwd + rel */
    int clen = (int)strlen(g_cwd);
    strncpy(dst, g_cwd, (size_t)(dst_size - 1));
    dst[dst_size - 1] = '\0';
    if (clen > 0 && dst[clen-1] != '/' && clen < dst_size - 1) {
        dst[clen++] = '/';
        dst[clen] = '\0';
    }
    strncat(dst, rel, (size_t)(dst_size - clen - 1));
}

/* ── help 명령 ──────────────────────────────────────────────────────── */
static void cmd_help(void) {
    puts("\nParinOS 유저 셸 명령:");
    puts("  help              - 도움말");
    puts("  exit [code]       - 셸 종료");
    puts("  echo <text>       - 텍스트 출력");
    puts("  cd <path>         - 디렉터리 변경");
    puts("  pwd               - 현재 디렉터리 출력");
    puts("  cat <file>        - 파일 내용 출력");
    puts("  run <elf> [args]  - ELF 프로그램 실행");
    puts("  /<path> [args]    - ELF 프로그램 직접 실행");
    puts("");
}

/* ── cat 명령 ────────────────────────────────────────────────────────── */
static void cmd_cat(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "cat: %s: 파일을 열 수 없습니다\n", path);
        return;
    }
    char buf[512];
    while (fgets(buf, sizeof(buf), f)) {
        fputs(buf, stdout);
    }
    fclose(f);
}

/* ── run 명령 ────────────────────────────────────────────────────────── */
static void cmd_run(int argc, const char **argv) {
    if (argc < 1) { puts("사용법: run <경로> [인수...]"); return; }

    char abs_path[256];
    resolve_path(argv[0], abs_path, sizeof(abs_path));

    printf("실행: %s (인수 %d개)\n", abs_path, argc);
    int ret = execve(abs_path, argc, argv);
    if (ret != 0) {
        fprintf(stderr, "실행 실패: %s\n", abs_path);
    }
}

/* ── 메인 루프 ───────────────────────────────────────────────────────── */
int main(int argc, const char **argv) {
    (void)argc; (void)argv;

    puts("\n  ParinOS 유저 셸  (help 입력 시 명령 목록)");
    puts("  종료: exit\n");

    char line[CMD_MAX];
    const char *cmd_argv[ARGV_MAX];

    while (1) {
        print_prompt();

        /* 한 줄 읽기 */
        int n = read(STDIN_FILENO, line, sizeof(line) - 1);
        if (n <= 0) continue;
        line[n] = '\0';
        trim(line);
        if (line[0] == '\0') continue;

        /* 인수 파싱 */
        int cmd_argc = parse_args(line, cmd_argv, ARGV_MAX);
        if (cmd_argc == 0) continue;

        const char *cmd = cmd_argv[0];

        /* 명령 처리 */
        if (strcmp(cmd, "exit") == 0) {
            int code = (cmd_argc > 1) ? atoi(cmd_argv[1]) : 0;
            printf("셸 종료 (코드 %d)\n", code);
            exit(code);
        } else if (strcmp(cmd, "help") == 0) {
            cmd_help();
        } else if (strcmp(cmd, "echo") == 0) {
            for (int i = 1; i < cmd_argc; i++) {
                if (i > 1) putchar(' ');
                fputs(cmd_argv[i], stdout);
            }
            putchar('\n');
        } else if (strcmp(cmd, "pwd") == 0) {
            puts(g_cwd);
        } else if (strcmp(cmd, "cd") == 0) {
            if (cmd_argc < 2) {
                strncpy(g_cwd, "/", sizeof(g_cwd) - 1);
            } else {
                char new_cwd[256];
                resolve_path(cmd_argv[1], new_cwd, sizeof(new_cwd));
                strncpy(g_cwd, new_cwd, sizeof(g_cwd) - 1);
                g_cwd[sizeof(g_cwd) - 1] = '\0';
            }
        } else if (strcmp(cmd, "cat") == 0) {
            if (cmd_argc < 2) { puts("사용법: cat <파일>"); }
            else {
                char abs_path[256];
                resolve_path(cmd_argv[1], abs_path, sizeof(abs_path));
                cmd_cat(abs_path);
            }
        } else if (strcmp(cmd, "run") == 0) {
            /* run <path> [args...] */
            cmd_run(cmd_argc - 1, cmd_argv + 1);
        } else if (cmd[0] == '/') {
            /* 절대 경로 직접 실행 */
            cmd_run(cmd_argc, cmd_argv);
        } else {
            /* /bin/ 에서 프로그램 검색 후 원래 인수 전달 */
            char bin_path[256];
            snprintf(bin_path, sizeof(bin_path), "/bin/%s", cmd);
            /* 첫 번째 인수(명령어 이름)를 절대 경로로 교체 */
            cmd_argv[0] = bin_path;
            cmd_run(cmd_argc, cmd_argv);
        }
    }

    return 0;
}
