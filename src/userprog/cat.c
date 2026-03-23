/*
 * cat.c — cat 유저 프로그램 (src/userprog/cat.c)
 *
 * 사용법: cat <파일> [파일2 ...]
 * 파일 내용을 stdout에 출력합니다. 파일이 없으면 오류 메시지를 출력합니다.
 */

#include "include/stdio.h"
#include "include/stdlib.h"
#include "include/string.h"

int main(int argc, const char **argv) {
    if (argc < 2) {
        fputs("사용법: cat <파일> [파일2 ...]\n", stderr);
        return 1;
    }

    int ret = 0;
    for (int i = 1; i < argc; i++) {
        FILE *f = fopen(argv[i], "r");
        if (!f) {
            fprintf(stderr, "cat: %s: 파일을 열 수 없습니다\n", argv[i]);
            ret = 1;
            continue;
        }

        char buf[512];
        while (fgets(buf, sizeof(buf), f)) {
            fputs(buf, stdout);
        }
        fclose(f);
    }
    return ret;
}
