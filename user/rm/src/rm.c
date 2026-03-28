/*
 * rm.c — rm 유저 프로그램
 *
 * 사용법: rm <파일> [파일2 ...]
 * 파일을 삭제합니다.
 */

#include "stdio.h"
#include "stdlib.h"
#include "unistd.h"

int main(int argc, const char **argv) {
    if (argc < 2) {
        fputs("사용법: rm <파일> [파일2 ...]\n", stderr);
        return 1;
    }

    int ret = 0;
    for (int i = 1; i < argc; i++) {
        int r = unlink(argv[i]);
        if (r != 0) {
            fprintf(stderr, "rm: %s: 파일을 삭제할 수 없습니다\n", argv[i]);
            ret = 1;
        }
    }
    return ret;
}
