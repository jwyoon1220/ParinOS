/*
 * cp.c — cp 유저 프로그램
 *
 * 사용법: cp <원본> <대상>
 * 파일을 복사합니다.
 */

#include "stdio.h"
#include "stdlib.h"
#include "string.h"

int main(int argc, const char **argv) {
    if (argc != 3) {
        fputs("사용법: cp <원본> <대상>\n", stderr);
        return 1;
    }

    FILE *src = fopen(argv[1], "r");
    if (!src) {
        fprintf(stderr, "cp: %s: 파일을 열 수 없습니다\n", argv[1]);
        return 1;
    }

    FILE *dst = fopen(argv[2], "w");
    if (!dst) {
        fprintf(stderr, "cp: %s: 파일을 만들 수 없습니다\n", argv[2]);
        fclose(src);
        return 1;
    }

    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
        fwrite(buf, 1, n, dst);
    }

    fclose(src);
    fclose(dst);
    return 0;
}
