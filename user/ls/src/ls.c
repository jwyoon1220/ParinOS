/*
 * ls.c — ls 유저 프로그램
 *
 * 사용법: ls [경로]
 * 디렉터리 내용을 나열합니다.
 */

#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "dirent.h"

int main(int argc, const char **argv) {
    const char *path = (argc > 1) ? argv[1] : "/";

    int fd = opendir_fd(path);
    if (fd < 0) {
        fprintf(stderr, "ls: %s: 디렉터리를 열 수 없습니다\n", path);
        return 1;
    }

    printf("디렉터리: %s\n", path);
    printf("%-24s %10s  %s\n", "이름", "크기", "타입");
    printf("%-24s %10s  %s\n", "────────────────────────", "──────────", "────");

    struct dirent ent;
    while (readdir_r(fd, &ent) == 0) {
        if (ent.d_type == DT_DIR) {
            printf("\033[34m%-24s\033[0m %10s  dir\n", ent.d_name, "-");
        } else {
            printf("%-24s %10u  file\n", ent.d_name, ent.d_size);
        }
    }

    closedir_fd(fd);
    return 0;
}
