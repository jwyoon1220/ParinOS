/*
 * echo.c — echo 유저 프로그램 (src/userprog/echo.c)
 *
 * 사용법: echo [텍스트...]
 * 인수를 공백으로 구분하여 stdout에 출력하고 개행합니다.
 */

#include "include/stdio.h"
#include "include/stdlib.h"

int main(int argc, const char **argv) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) putchar(' ');
        fputs(argv[i], stdout);
    }
    putchar('\n');
    return 0;
}
