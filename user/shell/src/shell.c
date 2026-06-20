/*
 * shell.c — ParinOS 유저 모드 셸
 *
 * 커널 셸과 독립적으로 동작하는 유저 프로그램 셸입니다.
 * int 0x80 시스템 콜을 통해서만 커널과 통신합니다.
 *
 * 실행: /bin/shell
 */

#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "unistd.h"
#include "syscall.h"


#define CMD_MAX   256
#define ARGV_MAX   32

/* ── 현재 작업 디렉터리 ─────────────────────────────────────────────── */
static char g_cwd[256] = "/";

/* ── 프롬프트 출력 ───────────────────────────────────────────────────── */
static void print_prompt(void) {
    printf("user@ParinOS:%s$ ", g_cwd);
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
    char temp[512];

    if (rel[0] == '/') {
        temp[0] = '\0';
    } else {
        strncpy(temp, g_cwd, sizeof(temp) - 1);
        temp[sizeof(temp) - 1] = '\0';
    }

    /* Tokenize rel and build the path */
    const char *p = rel;
    while (*p) {
        /* skip leading slashes */
        while (*p == '/') p++;
        if (!*p) break;

        /* extract next token */
        const char *start = p;
        while (*p && *p != '/') p++;
        int len = (int)(p - start);

        if (len == 1 && start[0] == '.') {
            /* do nothing */
        } else if (len == 2 && start[0] == '.' && start[1] == '.') {
            /* pop parent */
            int tlen = (int)strlen(temp);
            if (tlen > 1 && temp[tlen - 1] == '/') {
                temp[--tlen] = '\0';
            }
            while (tlen > 0 && temp[tlen - 1] != '/') {
                tlen--;
            }
            if (tlen == 0) {
                temp[0] = '/';
                temp[1] = '\0';
            } else {
                temp[tlen] = '\0';
            }
        } else {
            /* append token */
            int tlen = (int)strlen(temp);
            if (tlen == 0 || temp[tlen - 1] != '/') {
                if (tlen < (int)sizeof(temp) - 1) {
                    temp[tlen++] = '/';
                    temp[tlen] = '\0';
                }
            }
            if (tlen + len < (int)sizeof(temp)) {
                memcpy(temp + tlen, start, (size_t)len);
                temp[tlen + len] = '\0';
            }
        }
    }

    /* If empty, default to root "/" */
    if (temp[0] == '\0') {
        temp[0] = '/';
        temp[1] = '\0';
    }

    /* Ensure trailing slash is removed if not root "/" */
    int tlen = (int)strlen(temp);
    if (tlen > 1 && temp[tlen - 1] == '/') {
        temp[tlen - 1] = '\0';
    }

    strncpy(dst, temp, (size_t)(dst_size - 1));
    dst[dst_size - 1] = '\0';
}

/* ── help 명령 ──────────────────────────────────────────────────────── */
static void cmd_help(void) {
    puts("\nParinOS 셸 명령:");
    puts("  help              - 도움말");
    puts("  exit [code]       - 셸 종료");
    puts("  echo <text>       - 텍스트 출력");
    puts("  cd <path>         - 디렉터리 변경");
    puts("  pwd               - 현재 디렉터리 출력");
    puts("  cat <file>        - 파일 내용 출력 (/bin/cat)");
    puts("  ls [path]         - 디렉터리 목록 (/bin/ls)");
    puts("  mkdir <path>      - 디렉터리 생성 (/bin/mkdir)");
    puts("  rm <file>         - 파일 삭제 (/bin/rm)");
    puts("  cp <src> <dst>    - 파일 복사 (/bin/cp)");
    puts("  clear             - 화면 지우기 (cls)");
    puts("  free              - 커널 힙 메모리 상태 덤프");
    puts("  ps                - 커널 프로세스/스레드 정보 덤프");
    puts("  run <elf> [args]  - ELF 프로그램 실행");
    puts("  /<path> [args]    - 절대 경로로 직접 실행");
    puts("");
}

/* ── run 명령 ────────────────────────────────────────────────────────── */
static void cmd_run(int argc, const char **argv) {
    if (argc < 1) { puts("사용법: run <경로> [인수...]"); return; }

    char abs_path[256];
    resolve_path(argv[0], abs_path, sizeof(abs_path));

    /* 인자 경로 변환을 위한 로컬 버퍼들 */
    char resolved_args[ARGV_MAX][256];
    const char *new_argv[ARGV_MAX];
    int new_argc = argc;

    new_argv[0] = abs_path;

    const char *prog_name = argv[0];
    const char *last_slash = strrchr(prog_name, '/');
    if (last_slash) prog_name = last_slash + 1;

    if (strcmp(prog_name, "ls") == 0) {
        if (argc == 1) {
            strncpy(resolved_args[1], g_cwd, sizeof(resolved_args[1]) - 1);
            resolved_args[1][sizeof(resolved_args[1]) - 1] = '\0';
            new_argv[1] = resolved_args[1];
            new_argv[2] = NULL;
            new_argc = 2;
        } else {
            resolve_path(argv[1], resolved_args[1], sizeof(resolved_args[1]));
            new_argv[1] = resolved_args[1];
            for (int i = 2; i < argc; i++) {
                new_argv[i] = argv[i];
            }
            new_argv[argc] = NULL;
        }
    } else if (strcmp(prog_name, "cat") == 0 || strcmp(prog_name, "rm") == 0 || strcmp(prog_name, "mkdir") == 0) {
        for (int i = 1; i < argc; i++) {
            resolve_path(argv[i], resolved_args[i], sizeof(resolved_args[i]));
            new_argv[i] = resolved_args[i];
        }
        new_argv[argc] = NULL;
    } else if (strcmp(prog_name, "cp") == 0) {
        if (argc > 1) {
            resolve_path(argv[1], resolved_args[1], sizeof(resolved_args[1]));
            new_argv[1] = resolved_args[1];
        }
        if (argc > 2) {
            resolve_path(argv[2], resolved_args[2], sizeof(resolved_args[2]));
            new_argv[2] = resolved_args[2];
        }
        for (int i = 3; i < argc; i++) {
            new_argv[i] = argv[i];
        }
        new_argv[argc] = NULL;
    } else if (strcmp(prog_name, "gzip") == 0) {
        if (argc > 1) {
            if (argv[1][0] == '-') {
                new_argv[1] = argv[1];
                if (argc > 2) {
                    resolve_path(argv[2], resolved_args[2], sizeof(resolved_args[2]));
                    new_argv[2] = resolved_args[2];
                }
                for (int i = 3; i < argc; i++) {
                    new_argv[i] = argv[i];
                }
            } else {
                resolve_path(argv[1], resolved_args[1], sizeof(resolved_args[1]));
                new_argv[1] = resolved_args[1];
                for (int i = 2; i < argc; i++) {
                    new_argv[i] = argv[i];
                }
            }
        }
        new_argv[argc] = NULL;
    } else if (strcmp(prog_name, "jvm") == 0) {
        if (argc > 1) {
            if (strcmp(argv[1], "-jar") == 0 || strcmp(argv[1], "-jl") == 0) {
                new_argv[1] = argv[1];
                if (argc > 2) {
                    resolve_path(argv[2], resolved_args[2], sizeof(resolved_args[2]));
                    new_argv[2] = resolved_args[2];
                }
                for (int i = 3; i < argc; i++) {
                    new_argv[i] = argv[i];
                }
            } else {
                resolve_path(argv[1], resolved_args[1], sizeof(resolved_args[1]));
                new_argv[1] = resolved_args[1];
                for (int i = 2; i < argc; i++) {
                    new_argv[i] = argv[i];
                }
            }
        }
        new_argv[argc] = NULL;
    } else {
        /* 그 외 프로그램: 인수를 그대로 전달 */
        for (int i = 1; i < argc; i++) {
            new_argv[i] = argv[i];
        }
        new_argv[argc] = NULL;
    }

    int ret = execve(abs_path, new_argc, new_argv);
    if (ret != 0) {
        fprintf(stderr, "실행 실패: %s\n", abs_path);
    }
}

/* ── 메인 루프 ───────────────────────────────────────────────────────── */
int main(int argc, const char **argv) {
    (void)argc; (void)argv;

    puts("\n  ParinOS Shell  (type 'help' for commands)");
    puts("  exit to quit\n");

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
            exit(code);
        } else if (strcmp(cmd, "help") == 0) {
            cmd_help();
        } else if (strcmp(cmd, "clear") == 0 || strcmp(cmd, "cls") == 0) {
            syscall0(SYS_CLEAR);
        } else if (strcmp(cmd, "free") == 0) {
            syscall0(SYS_DUMP_HEAP);
        } else if (strcmp(cmd, "ps") == 0) {
            syscall0(SYS_DUMP_THREADS);
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
        } else if (strcmp(cmd, "run") == 0) {
            cmd_run(cmd_argc - 1, cmd_argv + 1);
        } else if (cmd[0] == '/') {
            /* 절대 경로 직접 실행 */
            cmd_run(cmd_argc, cmd_argv);
        } else {
            /* /bin/ 에서 프로그램 검색 */
            char bin_path[256];
            snprintf(bin_path, sizeof(bin_path), "/bin/%s", cmd);
            cmd_argv[0] = bin_path;
            cmd_run(cmd_argc, cmd_argv);
        }
    }

    return 0;
}
