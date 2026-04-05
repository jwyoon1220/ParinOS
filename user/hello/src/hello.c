/*
 * hello.c - ParinOS Ring 3 데모 프로그램
 *
 * 이 프로그램은 sysenter 시스템 콜을 통해 커널과 통신하며,
 * VFS 기반 파일 읽기, 디렉터리 목록, 프로세스 ID 조회를 시연합니다.
 *
 * 빌드: user/Makefile 에서 'hello' 타겟으로 자동 빌드됩니다.
 * 실행: 셸에서 '/bin/hello' 또는 'hello' 입력
 */

#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "unistd.h"

/* ── VFS 디렉터리 열기/읽기 시스콜 ─────────────────────────────────── */
static inline int opendir_sc(const char *path) {
    return syscall1(SYS_OPENDIR, (int)path);
}

struct dirent {
    char     d_name[256];
    unsigned d_size;
    unsigned char d_type;  /* 1=파일, 2=디렉터리 */
};

static inline int readdir_sc(int fd, struct dirent *ent) {
    return syscall2(SYS_READDIR, fd, (int)ent);
}

static inline int closedir_sc(int fd) {
    return syscall1(SYS_CLOSEDIR, fd);
}

/* ── 헬퍼 ───────────────────────────────────────────────────────────── */
static void print_line(const char *s) {
    int len = 0;
    while (s[len]) len++;
    write(STDOUT_FILENO, s, len);
    write(STDOUT_FILENO, "\n", 1);
}

/* ── 1. PID 출력 ─────────────────────────────────────────────────────── */
static void demo_getpid(void) {
    int pid = getpid();
    printf("[hello] 현재 프로세스 PID: %d\n", pid);
}

/* ── 2. /bin 디렉터리 목록 ─────────────────────────────────────────── */
static void demo_ls_bin(void) {
    puts("[hello] /bin 디렉터리 목록:");

    int fd = opendir_sc("/bin");
    if (fd < 0) {
        printf("  opendir 실패 (err=%d)\n", fd);
        return;
    }

    struct dirent ent;
    int count = 0;
    while (readdir_sc(fd, &ent) == 0) {
        char type_ch = (ent.d_type == 2) ? 'd' : '-';
        printf("  %c  %-20s  %u bytes\n", type_ch, ent.d_name, ent.d_size);
        count++;
    }
    closedir_sc(fd);
    printf("  총 %d 항목\n", count);
}

/* ── 3. 파일 내용 읽기 (/bin/hello 자신을 raw bytes 로 표시) ─────── */
static void demo_read_file(void) {
    const char *path = "/bin/hello";
    int fd = open(path, O_RDONLY, 0);
    if (fd < 0) {
        printf("[hello] %s 열기 실패 (err=%d)\n", path, fd);
        return;
    }

    /* 파일 크기 확인 (lseek SEEK_END) */
    int size = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);

    printf("[hello] %s 파일 크기: %d 바이트 (ELF 헤더 앞 4 바이트 출력)\n", path, size);

    char buf[4];
    int n = read(fd, buf, 4);
    if (n == 4) {
        printf("  매직: 0x%02x 0x%02x 0x%02x 0x%02x",
               (unsigned char)buf[0], (unsigned char)buf[1],
               (unsigned char)buf[2], (unsigned char)buf[3]);
        if (buf[0] == 0x7f && buf[1] == 'E' &&
            buf[2] == 'L'  && buf[3] == 'F') {
            puts("  → 유효한 ELF 바이너리 ✓");
        } else {
            puts("");
        }
    }
    close(fd);
}

/* ── main ────────────────────────────────────────────────────────────── */
int main(int argc, const char **argv) {
    (void)argc; (void)argv;

    puts("\n========================================");
    puts("  ParinOS Ring 3 데모 프로그램 (hello)");
    puts("  시스콜: sysenter / VFS 통신");
    puts("========================================\n");

    demo_getpid();
    puts("");
    demo_ls_bin();
    puts("");
    demo_read_file();
    puts("");

    print_line("[hello] 데모 완료. Ring 3 사용자 프로세스 정상 종료.");

    return 0;
}
