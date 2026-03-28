/*
 * mkdir.c — mkdir 유저 프로그램
 *
 * 사용법: mkdir <경로> [경로2 ...]
 * 디렉터리를 생성합니다.
 */

#include "stdio.h"
#include "stdlib.h"
#include "unistd.h"

int main(int argc, const char **argv) {
    if (argc < 2) {
        fputs("사용법: mkdir <경로> [경로2 ...]\n", stderr);
        return 1;
    }

    int ret = 0;
    for (int i = 1; i < argc; i++) {
        int r = mkdir(argv[i], 0755);
        if (r != 0) {
            fprintf(stderr, "mkdir: %s: 디렉터리를 만들 수 없습니다\n", argv[i]);
            ret = 1;
        }
    }
    return ret;
}
